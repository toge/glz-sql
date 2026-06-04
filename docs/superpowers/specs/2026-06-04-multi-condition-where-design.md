# Multi-Condition WHERE Design Spec

## Overview

`glz_sql::sql_repository<T>` の `select_by` / `find_by` / `update_by` / `remove_by` を、**コンパイル時に検証された複数条件 (AND / OR)** を受け付けられるよう拡張する。

条件オブジェクトを `where_eq<"col">(v)` のようなファクトリで生成し、`&&` / `||` で合成してリポジトリメソッドに渡す。sqlgen のようなフル DSL には踏み込まず、既存 API の自然な延長線上で表現力と安全性を両立する。

## Goals

- 既存単一条件 API (NTTP 化済み) は **削除** し、複数条件 API に統一
- カラム名は引き続き NTTP で受け取り、`valid_column` コンセプトでコンパイル時検証
- 比較演算子は **eq, ne, lt, le, gt, ge, like, IN, BETWEEN, IS NULL, IS NOT NULL** の 11 種類
- AND / OR の合成は `&&` / `||` 演算子で表現
- 既存 4 関数 (`select_by` / `find_by` / `update_by` / `remove_by`) すべてを新 API に対応

## Non-Goals (out of scope)

- `ORDER BY` / `LIMIT` / `OFFSET` / `GROUP BY` / `HAVING`
- テーブル JOIN / サブクエリ
- トランザクション制御 (既存 `database_interface` の枠外)
- SQL の生文字列実行 (エスケープ等の複雑性を持ち込むため)
- 既存単一条件 API との後方互換 (削除する)

## Tech Stack

- C++26 (既存)
- Glaze (リフレクション、既存)
- SQLite3 (既存)

## File Structure

```
include/glaze_sql/
  condition.hpp        -- 新規。condition 型 + where_* ファクトリ + 演算子
  sql_repository.hpp   -- 修正。xxx_by 4 関数を新 API に置換
test/
  test_sql_repository.cpp -- 追記。Op 別 + 合成のテスト
```

`condition` 型を別ファイルに切り出すことで、リポジトリ層と直交性を保ち、将来的な再利用 (例: バリデータ層) も可能にする。

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│ ユーザコード                                               │
│   repo.select_by(                                          │
│     where_gt<"age">(18) && where_eq<"name">("Alice")       │
│   );                                                       │
└────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌────────────────────────────────────────────────────────────┐
│ condition 層 (condition.hpp)                              │
│                                                            │
│  condition<Op, A1[, A2[, A3]]>                            │
│    - Op: 比較演算子 (enum class compare_op)               │
│    - Leaf: A1 = fixed_string<>, A2 = V, A3 = (optional)  │
│    - Composite: A1 = L condition, A2 = R condition       │
│                                                            │
│  ファクトリ: where_eq / where_ne / where_lt / ...         │
│             where_in / where_between / where_is_null      │
│                                                            │
│  演算子: operator&& (AND) / operator|| (OR)               │
└────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌────────────────────────────────────────────────────────────┐
│ リポジトリ層 (sql_repository.hpp)                          │
│                                                            │
│  select_by(Cond) / find_by(Cond)                         │
│  update_by(record, Cond) / remove_by(Cond)               │
│    - 1) Cond を再帰的に fragment 生成                      │
│    - 2) Cond を再帰的に bind (次の index を返す)          │
│    - 3) SQL: SELECT ... WHERE <frag> ;                    │
│    - 4) UPDATE: SET バインド → WHERE バインド            │
└────────────────────────────────────────────────────────────┘
```

## `condition` クラス

### 型定義

```cpp
namespace glz_sql {

/**
 * @brief サポートされる比較演算子
 */
enum class compare_op {
  eq, ne, lt, le, gt, ge,   // 基本 6 種
  like,                      // 部分一致
  in_op, between,            // 複数値
  is_null, is_not_null,      // NULL 判定
  and_op, or_op              // 論理合成
};

/**
 * @brief WHERE 句の単一条件または合成条件を表現するクラス
 *
 * Leaf:   A1 = fixed_string<N>, A2 = V[, A3 = V]
 *         (eq, ne, lt, le, gt, ge, like, in_op, between,
 *          is_null, is_not_null)
 * Composite: A1 = L, A2 = R
 *         (and_op, or_op)
 *
 * ファクトリ関数 (where_eq 等) 以外から直接構築することは想定しない。
 */
template <compare_op Op, typename A1, typename A2 = void, typename A3 = void>
class condition;

}  // namespace glz_sql
```

各 Op ごとに特殊化 (または本体で `if constexpr` ディスパッチ) し、以下の 3 つの操作を提供:

```cpp
class condition {
public:
  /**
   * @brief SQL フラグメントを生成する (例: "age > ?", "id IN (?,?,?)", "(a = ?) AND (b = ?)")
   * @return フラグメント文字列
   */
  auto fragment() const -> std::string;

  /**
   * @brief プレースホルダ数 (例: eq なら 1、IN 3 つなら 3、BETWEEN なら 2、IS NULL なら 0)
   */
  static constexpr auto placeholder_count() -> size_t;

  /**
   * @brief 値をバインドし、次のバインド位置 (1-indexed) を返す
   * @param stmt ステートメント
   * @param start_index バインド開始位置 (1-indexed)
   * @return 次のバインド位置
   */
  auto bind(sqlite3_stmt* stmt, int start_index) const -> int;
};
```

### ファクトリ関数

```cpp
namespace glz_sql {

// カラム検証はコンセプト valid_column で行う。
// where_* ファクトリ自体はテーブル T を引数に取らないため、検証は
// repository メソッド呼び出し時の valid_condition コンセプトで行う。
template <fixed_string C, typename V>
auto where_eq(const V& v) -> condition<compare_op::eq, fixed_string_of<C>, V>;

template <fixed_string C, typename V>
auto where_ne(const V& v) -> condition<compare_op::ne, fixed_string_of<C>, V>;

// where_lt, where_le, where_gt, where_ge も同様

template <fixed_string C, typename V>
auto where_like(const V& v) -> condition<compare_op::like, fixed_string_of<C>, V>;

/**
 * @brief IN 条件 (可変長)
 *
 * すべての値は同一型 V であること (異なる型を混在させるとコンパイル時エラー)。
 */
template <fixed_string C, typename V, typename... Vs>
  requires (std::same_as<V, Vs> && ...)
auto where_in(const V& first, const Vs&... rest)
  -> condition<compare_op::in_op, fixed_string_of<C>, std::tuple<V, Vs...>>;

/**
 * @brief BETWEEN 条件 (2 値)
 */
template <fixed_string C, typename V>
auto where_between(const V& low, const V& high)
  -> condition<compare_op::between, fixed_string_of<C>, V, V>;

/**
 * @brief IS NULL / IS NOT NULL
 */
template <fixed_string C>
auto where_is_null() -> condition<compare_op::is_null, fixed_string_of<C>>;

template <fixed_string C>
auto where_is_not_null() -> condition<compare_op::is_not_null, fixed_string_of<C>>;

}  // namespace glz_sql
```

`fixed_string_of<C>` は `fixed_string<C.size()>` 型を返すエイリアス (C は NTTP のため型化が必要)。

### 論理演算子

```cpp
/**
 * @brief 2 条件を AND で合成
 */
template <compare_op LOp, typename LA1, typename LA2, typename LA3,
          compare_op ROp, typename RA1, typename RA2, typename RA3>
auto operator&&(const condition<LOp, LA1, LA2, LA3>& l,
                const condition<ROp, RA1, RA2, RA3>& r)
  -> condition<compare_op::and_op, condition<LOp, LA1, LA2, LA3>, condition<ROp, RA1, RA2, RA3>>;

/**
 * @brief 2 条件を OR で合成
 */
template <compare_op LOp, typename LA1, typename LA2, typename LA3,
          compare_op ROp, typename RA1, typename RA2, typename RA3>
auto operator||(const condition<LOp, LA1, LA2, LA3>& l,
                const condition<ROp, RA1, RA2, RA3>& r)
  -> condition<compare_op::or_op, condition<LOp, LA1, LA2, LA3>, condition<ROp, RA1, RA2, RA3>>;
```

`&&` は `||` より C++ の優先順位が高い。SQL の `AND` / `OR` の優先順位と一致するため、括弧なしでも意味論が一致する。`fragment()` 実装では読みやすさのため `(left_frag) AND (right_frag)` のように括弧で囲む。

## Repository API の変更

### 新シグネチャ

```cpp
namespace glz_sql {

template <typename T>
  requires sql_table<T>
class sql_repository {
public:
  /**
   * @brief 複数条件で検索する (複数件)
   * @param cond 条件式 (where_* の合成)
   * @return レコードのベクタ
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto select_by(const Cond& cond) const -> std::vector<T>;

  /**
   * @brief 複数条件で検索する (1件)
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto find_by(const Cond& cond) const -> std::optional<T>;

  /**
   * @brief 複数条件でレコードを更新する
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto update_by(const T& record, const Cond& cond) const -> bool;

  /**
   * @brief 複数条件でレコードを削除する
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto remove_by(const Cond& cond) const -> bool;

  // create_table / insert / select_all は変更なし
};

}  // namespace glz_sql
```

### `valid_condition` コンセプト

`Cond` が Leaf の場合と Composite の場合で定義が変わる。実装ではテンプレート特殊化または `if constexpr` で分岐する。

```cpp
/**
 * @brief Cond が T のカラムに対する妥当な条件式であることを保証する
 *
 * Leaf:    Cond = condition<Op, fixed_string<N>, V[, V]>
 *          のとき valid_column<fixed_string<N>, T> が真であること。
 *
 * Composite: Cond = condition<and_op|or_op, L, R>
 *          のとき valid_condition<L, T> && valid_condition<R, T> が真であること。
 */
template <typename Cond, typename T>
concept valid_condition = /* 実装は特殊化／constexpr-if で上記ルールを判定 */;
```

実装方針:
- `Cond = condition<Op, A1, A2, A3>` の Leaf Op (`eq`..`is_not_null`) では `valid_column<A1, T>` を要求
- `Cond = condition<and_op|or_op, L, R>` の Composite Op では `valid_condition<L, T> && valid_condition<R, T>` を要求

### SQL 生成 (リポジトリ内部)

`select_by` / `find_by` / `remove_by` は以下の流れ:

```cpp
template <typename Cond>
auto sql_repository<T>::execute_select_where(const Cond& cond) -> std::vector<T> {
  // 1. フラグメント生成 (再帰的にツリーを辿る)
  std::string frag = cond.fragment();

  // 2. SQL 構築
  std::string sql = std::format(
    "SELECT {} FROM {} WHERE {};",
    join_field_names(), T::table_name, frag
  );

  // 3. ステートメント準備 + バインド + 実行
  auto stmt = db_.prepare(sql);
  if (stmt == nullptr) { /* エラー処理 */ return {}; }
  cond.bind(stmt.get(), 1);
  return fetch_all(stmt.get());
}
```

`update_by` は SET 句のフィールドを先にバインドし、続けて WHERE 句の条件をバインドする:

```cpp
template <typename Cond>
auto sql_repository<T>::update_by(const T& record, const Cond& cond) const -> bool {
  std::string frag = cond.fragment();
  std::string sql  = std::format(
    "UPDATE {} SET {} WHERE {};",
    T::table_name, set_clause(), frag
  );
  auto stmt = db_.prepare(sql);
  if (stmt == nullptr) { return false; }
  bind_fields(stmt.get(), record);                      // 1..field_count()
  cond.bind(stmt.get(), static_cast<int>(field_count()) + 1);
  /* step + エラー処理 */
  return true;
}
```

#### フラグメント生成ルール

| Op | フラグメント |
|---|---|
| `eq` | `col = ?` |
| `ne` | `col != ?` |
| `lt` | `col < ?` |
| `le` | `col <= ?` |
| `gt` | `col > ?` |
| `ge` | `col >= ?` |
| `like` | `col LIKE ?` |
| `in_op` | `col IN (?, ?, ?, ...)` (N 個) |
| `between` | `col BETWEEN ? AND ?` |
| `is_null` | `col IS NULL` |
| `is_not_null` | `col IS NOT NULL` |
| `and_op` | `(left.frag) AND (right.frag)` |
| `or_op` | `(left.frag) OR (right.frag)` |

#### バインド順

`update_by(record, cond)` の場合:

```
1. SET 句: 全フィールドを 1..field_count() にバインド
2. WHERE 句: cond.bind(stmt, field_count() + 1) で再帰的にバインド
```

`cond.bind(stmt, start)` は Leaf の場合は自身の値をバインドして `start + placeholder_count` を返し、Composite の場合は left.bind → right.bind の順で処理して最終位置を返す。

## Error Handling

| エラーケース | 検出タイミング | 挙動 |
|---|---|---|
| 存在しないカラム名 | コンパイル時 | `valid_column` コンセプト違反 → `static_assert` 相当のエラー |
| 値型の不一致 | コンパイル時 | `sqlite_type_traits<V>::bind` のインスタンス化失敗 |
| `IN` の引数 0 個 | コンパイル時 | `requires sizeof...(Vs) > 0` |
| `BETWEEN` の引数 ≠ 2 | ファクトリで制御 | シグネチャで 2 引数固定 |
| プレペアドステートメント失敗 | 実行時 | `db_.error_message()` を `std::cerr` に出力し、規定値を返す (`false` / `nullopt` / 空ベクタ) |
| `sqlite3_step` 失敗 | 実行時 | 同上 |

例外は投げない (既存方針に準拠)。

## Coding Rules

- 言語: C++26
- コメント: 日本語、Doxygen 形式 (`@brief`, `@param`, `@return`)
- インデント: 半角スペース 2 文字
- `condition` クラスのメンバー: `private` データ + `public` `const` メソッド
- 例外を投げない (既存方針)
- 既存 `sql_repository.hpp` のスタイル (`auto`, `auto const`, `std::format`, `std::cerr`) に揃える

## Testing Strategy

`test/test_sql_repository.cpp` に追記するテスト (TDD)。基本は `TEST_CASE` を Op ごとに分割。

### 共通準備

```cpp
struct User {
  int64_t id{};
  std::optional<std::string> email;
  std::string name;
  int64_t age{};
  double score{};
  static constexpr std::string_view table_name = "users";
};

template <> struct glz::meta<User> {
  using type = User;
  static constexpr auto value = object(
    "id", &User::id,
    "email", &User::email,
    "name", &User::name,
    "age", &User::age,
    "score", &User::score
  );
};
```

`email` は `std::optional` にして IS NULL テストに対応する。

### テストケース一覧

| # | テストケース | 検証内容 |
|---|---|---|
| 1 | `select_by` 基本 6 種 | eq / ne / lt / le / gt / ge それぞれの正常系 |
| 2 | `select_by` 0 件 | ヒットしない条件 |
| 3 | `select_by` AND 合成 | 2 条件 AND で正しい件数 |
| 4 | `select_by` OR 合成 | 2 条件 OR で正しい件数 |
| 5 | `select_by` AND + OR 混合 | 3 条件の優先順位確認 |
| 6 | `select_by` LIKE | 部分一致 |
| 7 | `select_by` IN | 複数値のいずれか |
| 8 | `select_by` BETWEEN | 範囲 |
| 9 | `select_by` IS NULL / IS NOT NULL | optional フィールド |
| 10 | `find_by` 全 Op | select_by と同じ Op を 1 件検索で確認 |
| 11 | `update_by` 単一条件 | 1 条件で更新 (旧 API と同じ) |
| 12 | `update_by` 複数条件 | AND / OR で正しく更新対象が絞られる |
| 13 | `update_by` バインド順 | SET 句と WHERE 句の値が混ざらないこと |
| 14 | `remove_by` 単一条件 | 1 条件で削除 |
| 15 | `remove_by` 複数条件 | 複数条件で削除 |
| 16 | コンパイル時ネガティブ | 不正カラム名を渡してコンパイル失敗することを確認 (コメントアウトしたコードで説明) |

### ビルド & テスト

```bash
./build.sh && cd build && ctest --output-on-failure
```

全件 PASS を成功条件とする。

## Coding Example

```cpp
glz_sql::sqlite_database db(":memory:");
glz_sql::sql_repository<User> repo(db);
repo.create_table();

// データ準備 (省略)

// 単一条件
auto adults = repo.select_by(where_gt<"age">(18));

// 複数条件 (AND)
auto result = repo.select_by(
  where_gt<"age">(18) && where_eq<"name">("Alice")
);

// 複数条件 (OR)
auto result2 = repo.select_by(
  where_lt<"age">(10) || where_gt<"age">(60)
);

// 混合 (AND は || より強く結合)
auto result3 = repo.select_by(
  where_gt<"age">(18) && where_eq<"name">("Alice") || where_eq<"name">("Bob")
);
// → WHERE (("age" > 18) AND ("name" = 'Alice')) OR ("name" = 'Bob')

// IN / BETWEEN / LIKE / NULL
auto in_set  = repo.select_by(where_in<"id">(1, 3, 5));
auto in_range = repo.select_by(where_between<"age">(20, 30));
auto partial = repo.select_by(where_like<"name">("Al%"));
auto no_email = repo.select_by(where_is_null<"email">());

// 更新 / 削除も同様
repo.update_by(new_user, where_eq<"id">(1));
repo.remove_by(where_lt<"age">(0) || where_is_null<"email">());
```

## Future Extensions (out of scope, 将来計画)

本 spec は WHERE 句の複数条件化に閉じた範囲に留める。ただし以下の機能はこの実装を土台として非破壊的に追加可能。

### ORDER BY / LIMIT / OFFSET

- 影響範囲: 既存 `condition` 型および `xxx_by` メソッドには触らない。新メソッドを追加する。
- 想定 API (一例):

  ```cpp
  // 方法 1: 新メソッドで追加 (既存 select_by はそのまま)
  template <fixed_string Col, typename Cond>
  auto select_ordered(const Cond& cond, order_direction dir = asc) const -> std::vector<T>;

  // 方法 2: 戻り値をクエリオブジェクトに変更 (より高機能)
  auto q = repo.select_by(where_gt<"age">(18));
  q.order_by<"name">(desc).limit(10);
  auto rows = q.fetch();
  ```
- 実装ヒント: `order_by<"col">(asc|desc)` で `order_spec` オブジェクトを生成し、`select_ordered` の引数に渡す。`fragment()` ヘルパで `"ORDER BY col ASC"` 等を生成。
- 互換性: 既存呼び出しコードは変更不要。

### GROUP BY / HAVING + 集約関数

- 影響範囲: 行指向 (`std::vector<T>`) ではなく集約結果 (任意 shape の構造体) を返すため、リポジトリに新メソッド群を追加。
- 想定 API:

  ```cpp
  // 集約結果の構造体 (Glaze メタでフィールド定義)
  struct AgeStats {
    std::string name;
    int64_t count{};
    double avg_age{};
  };
  template <> struct glz::meta<AgeStats> { /* ... */ };

  // 集約クエリ
  auto stats = repo.select_grouped<AgeStats>(
    where_gt<"age">(0),
    group_by<"name">(),
    count().as<"count">(),
    avg<"age">().as<"avg_age">()
  );
  ```
- 新ファイル: `include/glaze_sql/aggregate.hpp` に `count()` / `sum()` / `avg()` / `min()` / `max()` ファクトリ。
- 互換性: 既存 `select_by` / `find_by` は無影響。

### トランザクション

- 影響範囲: `condition` / WHERE とは完全に独立。`database_interface` の拡張のみ。
- 想定 API:

  ```cpp
  // database.hpp への追加
  class database_interface {
    virtual auto begin() -> bool = 0;
    virtual auto commit() -> bool = 0;
    virtual auto rollback() -> bool = 0;
  };

  // 使用例
  db.begin();
  repo.update_by(record, where_eq<"id">(1));
  repo.remove_by(where_lt<"age">(0));
  db.commit();  // または rollback()
  ```
- 互換性: 既存 `execute()` / `prepare()` には触らない。SQLite 実装側に `sqlite3_exec("BEGIN")` などを追加するだけ。

### 拡張時の設計メモ

- 本実装では `select_by` / `find_by` の戻り値を直接 `std::vector<T>` / `std::optional<T>` としている。ORDER BY を「方法 2 (クエリオブジェクト化)」で追加する場合は、既存メソッドを残したまま新 `query` 型を段階導入することで非破壊的に移行可能。
- `condition` 型は SQL フラグメントとバインド値の 2 責務だけを持つ最小 API に保っているため、JOIN やサブクエリを将来追加する場合も `condition` 自体を拡張せず、新しい `join_spec` などの型を並置すれば済む。
- 新機能追加時も `valid_column` / `valid_condition` コンセプトの流用が可能。
```
