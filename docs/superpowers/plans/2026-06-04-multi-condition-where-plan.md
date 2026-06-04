# Multi-Condition WHERE Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `sql_repository<T>` の `select_by` / `find_by` / `update_by` / `remove_by` を、`where_*` ファクトリと `&&` / `||` で合成した複数条件 (AND / OR) を受け取れるよう拡張する。比較演算子 11 種 (eq, ne, lt, le, gt, ge, like, IN, BETWEEN, IS NULL, IS NOT NULL) をサポート。

**Architecture:**
- 新ヘッダ `include/glaze_sql/condition.hpp` に `leaf_condition<Op, Col, V, V2>` / `composite_condition<Op, L, R>` クラスを定義
- `where_eq` / `where_ne` / ... / `where_in` / `where_between` / `where_is_null` / `where_is_not_null` ファクトリを提供
- `operator&&` / `operator||` で 2 条件を AND / OR 合成
- `valid_condition<Cond, T>` コンセプトで全葉のカラム名を T のフィールドに対してコンパイル時検証
- `sql_repository<T>` の `xxx_by` 4 関数を新 API に置換 (旧単一条件 API は削除)

**Tech Stack:** C++26, Glaze, SQLite3, Catch2 (テスト)

---

## 変更概要

### 新規ファイル

```
include/glaze_sql/condition.hpp  -- condition 型 + where_* ファクトリ + 演算子
```

### 変更ファイル

```
include/glaze_sql/sqlite_bind.hpp  -- std::optional<T> の特殊化を追加 (IS NULL テスト用)
include/glaze_sql/sql_repository.hpp  -- xxx_by 4 関数を新 API に置換
test/test_sql_repository.cpp  -- 既存テスト移行 + 新規テスト追加
```

### `condition.hpp` の型設計

```cpp
namespace glz_sql {

enum class compare_op {
  eq, ne, lt, le, gt, ge,    // 基本 6 種
  like,                       // 部分一致
  in_op, between,             // 複数値
  is_null, is_not_null,       // NULL 判定
  and_op, or_op               // 論理合成
};

/**
 * @brief 葉条件 (比較 1 つ + 値)
 */
template <compare_op Op, fixed_string C, typename V = void, typename V2 = void>
class leaf_condition {
  static constexpr auto column = C;
  V value_;
  V2 value2_;
  // fragment() / bind() / placeholder_count() を if constexpr で Op ごとにディスパッチ
};

/**
 * @brief 合成条件 (2 つの子条件 + AND/OR)
 */
template <compare_op Op, typename L, typename R>
  requires (Op == compare_op::and_op || Op == compare_op::or_op)
class composite_condition {
  L left_;
  R right_;
  // fragment() / bind() / placeholder_count() を再帰的に処理
};

}  // namespace glz_sql
```

---

## タスク定義

### Task 1: std::optional の特殊化追加

**Files:**
- Modify: `include/glaze_sql/sqlite_bind.hpp`

- [ ] **Step 1: 特殊化を追加**

ファイルの末尾 (`std::string_view` の特殊化の後) に以下を追加:

```cpp
/**
 * @brief std::optional<T> の特殊化: NULL 許容カラム用
 *
 * std::nullopt を NULL として bind し、SQLite からの NULL を std::nullopt として読み取る。
 */
template <typename T>
struct sqlite_type_traits<std::optional<T>> {
  static constexpr const char* sql_type = sqlite_type_traits<T>::sql_type;

  static auto bind(sqlite3_stmt* stmt, int index, const std::optional<T>& value) -> int {
    if (!value.has_value()) {
      return sqlite3_bind_null(stmt, index);
    }
    return sqlite_type_traits<T>::bind(stmt, index, *value);
  }

  static auto column(sqlite3_stmt* stmt, int index) -> std::optional<T> {
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) {
      return std::nullopt;
    }
    return sqlite_type_traits<T>::column(stmt, index);
  }
};
```

- [ ] **Step 2: ビルド確認**

Run: `./build.sh`
Expected: 成功 (変更は追加のみ)

- [ ] **Step 3: コミット**

```bash
git add include/glaze_sql/sqlite_bind.hpp
git commit -m "feat: add std::optional support to sqlite_type_traits"
```

---

### Task 2: condition.hpp 基本構造 (型とコンセプト)

**Files:**
- Create: `include/glaze_sql/condition.hpp`

- [ ] **Step 1: ヘッダのスケルトンを作成**

```cpp
#pragma once

#include "database.hpp"
#include "sqlite_bind.hpp"
#include "sql_repository.hpp"  // fixed_string を使う

#include <concepts>
#include <format>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace glz_sql {

/**
 * @brief サポートされる比較演算子
 */
enum class compare_op {
  eq, ne, lt, le, gt, ge,    // 基本 6 種
  like,                       // 部分一致
  in_op, between,             // 複数値
  is_null, is_not_null,       // NULL 判定
  and_op, or_op               // 論理合成
};

/**
 * @brief 比較演算子が葉 (リーフ条件) かどうか
 */
constexpr auto is_leaf_op(compare_op op) -> bool {
  return op == compare_op::eq || op == compare_op::ne
      || op == compare_op::lt || op == compare_op::le
      || op == compare_op::gt || op == compare_op::ge
      || op == compare_op::like
      || op == compare_op::in_op || op == compare_op::between
      || op == compare_op::is_null || op == compare_op::is_not_null;
}

/**
 * @brief 葉条件の前方宣言
 */
template <compare_op Op, fixed_string C, typename V = void, typename V2 = void>
class leaf_condition;

/**
 * @brief 合成条件の前方宣言
 */
template <compare_op Op, typename L, typename R>
class composite_condition;

/**
 * @brief 任意の condition 型 (葉または合成) であることを判定するコンセプト
 *
 * leaf_condition は `using is_leaf_condition_tag = void;` を、
 * composite_condition は `using is_composite_condition_tag = void;` を
 * 持つことでコンセプトが真になる。
 */
template <typename T>
concept any_condition = requires {
  typename T::is_leaf_condition_tag;
} || requires {
  typename T::is_composite_condition_tag;
};

}  // namespace glz_sql
```

- [ ] **Step 2: ビルド確認 (まだ他の型は空なので、ファイル単体では不完全で OK)**

Run: `./build.sh`
Expected: ビルド成功 (header-only、参照のみで実体化なし)

- [ ] **Step 3: コミット**

```bash
git add include/glaze_sql/condition.hpp
git commit -m "feat(condition): add compare_op enum and condition class skeleton"
```

---

### Task 3: leaf_condition の基本 6 種サポート

**Files:**
- Modify: `include/glaze_sql/condition.hpp`

- [ ] **Step 1: 失敗するテストを書く**

`test/test_sql_repository.cpp` の末尾に追加:

```cpp
#include "glaze_sql/condition.hpp"

TEST_CASE("condition: where_eq") {
  auto c = glz_sql::where_eq<"name">(std::string{"Alice"});
  auto frag = c.fragment();
  REQUIRE(frag == "name = ?");
  REQUIRE(c.placeholder_count() == 1);
}
```

- [ ] **Step 2: テストが失敗することを確認**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: `where_eq` が未定義のためコンパイル失敗

- [ ] **Step 3: leaf_condition クラスと where_eq を実装**

`condition.hpp` の末尾に以下を追加:

```cpp
/**
 * @brief 葉条件の本体
 */
template <compare_op Op, fixed_string C, typename V, typename V2>
class leaf_condition {
  static_assert(is_leaf_op(Op), "leaf_condition requires a leaf op");

  static constexpr auto column_view = std::string_view(C);

  V value_;
  V2 value2_;

public:
  using is_leaf_condition_tag = void;
  static constexpr compare_op op = Op;
  using column_type = decltype(C);

  constexpr leaf_condition() : value_{}, value2_{} {}
  explicit constexpr leaf_condition(V v) : value_(std::move(v)), value2_{} {}
  constexpr leaf_condition(V v, V2 v2) : value_(std::move(v)), value2_(std::move(v2)) {}

  /**
   * @brief プレースホルダ数
   */
  static constexpr auto placeholder_count() -> size_t {
    if constexpr (Op == compare_op::between) {
      return 2;
    } else if constexpr (Op == compare_op::is_null || Op == compare_op::is_not_null) {
      return 0;
    } else if constexpr (Op == compare_op::in_op) {
      return std::tuple_size_v<V>;
    } else {
      return 1;
    }
  }

  /**
   * @brief SQL フラグメントを生成
   */
  auto fragment() const -> std::string {
    if constexpr (Op == compare_op::eq) {
      return std::format("{} = ?", column_view);
    } else if constexpr (Op == compare_op::ne) {
      return std::format("{} != ?", column_view);
    } else if constexpr (Op == compare_op::lt) {
      return std::format("{} < ?", column_view);
    } else if constexpr (Op == compare_op::le) {
      return std::format("{} <= ?", column_view);
    } else if constexpr (Op == compare_op::gt) {
      return std::format("{} > ?", column_view);
    } else if constexpr (Op == compare_op::ge) {
      return std::format("{} >= ?", column_view);
    } else if constexpr (Op == compare_op::like) {
      return std::format("{} LIKE ?", column_view);
    } else if constexpr (Op == compare_op::in_op) {
      std::string frag = std::format("{} IN (", column_view);
      bool first = true;
      std::apply([&](auto const&... vs) {
        ((frag += std::string(first ? "?" : ", ?"), first = false), ...);
      }, value_);
      frag += ")";
      return frag;
    } else if constexpr (Op == compare_op::between) {
      return std::format("{} BETWEEN ? AND ?", column_view);
    } else if constexpr (Op == compare_op::is_null) {
      return std::format("{} IS NULL", column_view);
    } else if constexpr (Op == compare_op::is_not_null) {
      return std::format("{} IS NOT NULL", column_view);
    } else {
      static_assert(is_leaf_op(Op), "unsupported leaf op");
    }
  }

  /**
   * @brief 値をバインドし次のバインド位置を返す
   */
  auto bind(sqlite3_stmt* stmt, int start_index) const -> int {
    if constexpr (Op == compare_op::is_null || Op == compare_op::is_not_null) {
      return start_index;
    } else if constexpr (Op == compare_op::between) {
      int idx = start_index;
      sqlite_type_traits<V>::bind(stmt, idx, value_);
      sqlite_type_traits<V2>::bind(stmt, idx + 1, value2_);
      return idx + 2;
    } else if constexpr (Op == compare_op::in_op) {
      int idx = start_index;
      std::apply([&](auto const&... vs) {
        ((sqlite_type_traits<std::remove_cvref_t<decltype(vs)>>::bind(stmt, idx++, vs)), ...);
      }, value_);
      return idx;
    } else {
      sqlite_type_traits<V>::bind(stmt, start_index, value_);
      return start_index + 1;
    }
  }
};

/**
 * @brief where_eq ファクトリ
 */
template <fixed_string C, typename V>
auto where_eq(V v) -> leaf_condition<compare_op::eq, C, std::decay_t<V>> {
  return leaf_condition<compare_op::eq, C, std::decay_t<V>>(std::move(v));
}

}  // namespace glz_sql
```

注: `any_condition` コンセプトは Task 2 で既に正しく定義済み。`leaf_condition` に `is_leaf_condition_tag` を追加するだけでコンセプトが反応する。

- [ ] **Step 4: テストが通ることを確認**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 5: コミット**

```bash
git add include/glaze_sql/condition.hpp test/test_sql_repository.cpp
git commit -m "feat(condition): add leaf_condition with basic 6 ops and where_eq"
```

---

### Task 4: 残りの基本 5 種のファクトリ

**Files:**
- Modify: `include/glaze_sql/condition.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを追加**

`test_sql_repository.cpp` の末尾に追加:

```cpp
TEST_CASE("condition: basic 6 ops fragment") {
  using namespace glz_sql;

  REQUIRE((where_ne<"name">(std::string{"x"}).fragment()) == "name != ?");
  REQUIRE((where_lt<"age">(int64_t{10}).fragment()) == "age < ?");
  REQUIRE((where_le<"age">(int64_t{10}).fragment()) == "age <= ?");
  REQUIRE((where_gt<"age">(int64_t{10}).fragment()) == "age > ?");
  REQUIRE((where_ge<"age">(int64_t{10}).fragment()) == "age >= ?");

  REQUIRE(where_ne<"name">(std::string{"x"}).placeholder_count() == 1);
  REQUIRE(where_gt<"age">(int64_t{10}).placeholder_count() == 1);
}
```

- [ ] **Step 2: ファクトリを追加**

`condition.hpp` の `where_eq` の直後に追加:

```cpp
template <fixed_string C, typename V>
auto where_ne(V v) -> leaf_condition<compare_op::ne, C, std::decay_t<V>> {
  return leaf_condition<compare_op::ne, C, std::decay_t<V>>(std::move(v));
}

template <fixed_string C, typename V>
auto where_lt(V v) -> leaf_condition<compare_op::lt, C, std::decay_t<V>> {
  return leaf_condition<compare_op::lt, C, std::decay_t<V>>(std::move(v));
}

template <fixed_string C, typename V>
auto where_le(V v) -> leaf_condition<compare_op::le, C, std::decay_t<V>> {
  return leaf_condition<compare_op::le, C, std::decay_t<V>>(std::move(v));
}

template <fixed_string C, typename V>
auto where_gt(V v) -> leaf_condition<compare_op::gt, C, std::decay_t<V>> {
  return leaf_condition<compare_op::gt, C, std::decay_t<V>>(std::move(v));
}

template <fixed_string C, typename V>
auto where_ge(V v) -> leaf_condition<compare_op::ge, C, std::decay_t<V>> {
  return leaf_condition<compare_op::ge, C, std::decay_t<V>>(std::move(v));
}
```

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/condition.hpp test/test_sql_repository.cpp
git commit -m "feat(condition): add where_ne/lt/le/gt/ge factories"
```

---

### Task 5: LIKE Op

**Files:**
- Modify: `include/glaze_sql/condition.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを追加**

```cpp
TEST_CASE("condition: where_like") {
  using namespace glz_sql;
  REQUIRE((where_like<"name">(std::string{"Al%"}).fragment()) == "name LIKE ?");
  REQUIRE(where_like<"name">(std::string{"Al%"}).placeholder_count() == 1);
}
```

- [ ] **Step 2: ファクトリを追加**

```cpp
template <fixed_string C, typename V>
auto where_like(V v) -> leaf_condition<compare_op::like, C, std::decay_t<V>> {
  return leaf_condition<compare_op::like, C, std::decay_t<V>>(std::move(v));
}
```

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/condition.hpp test/test_sql_repository.cpp
git commit -m "feat(condition): add where_like factory"
```

---

### Task 6: IN Op

**Files:**
- Modify: `include/glaze_sql/condition.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを追加**

```cpp
TEST_CASE("condition: where_in") {
  using namespace glz_sql;
  auto c = where_in<"id">(int64_t{1}, int64_t{3}, int64_t{5});
  REQUIRE(c.fragment() == "id IN (?, ?, ?)");
  REQUIRE(c.placeholder_count() == 3);
}
```

- [ ] **Step 2: ファクトリを追加**

```cpp
/**
 * @brief IN 条件 (可変長、すべての値は同一型)
 */
template <fixed_string C, typename V, typename... Vs>
  requires (std::same_as<V, Vs> && ...)
auto where_in(V v, Vs... vs) -> leaf_condition<compare_op::in_op, C, std::tuple<std::decay_t<V>, std::decay_t<Vs>...>> {
  return leaf_condition<compare_op::in_op, C, std::tuple<std::decay_t<V>, std::decay_t<Vs>...>>(std::tuple<std::decay_t<V>, std::decay_t<Vs>...>(std::move(v), std::move(vs)...));
}
```

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/condition.hpp test/test_sql_repository.cpp
git commit -m "feat(condition): add where_in factory with variadic values"
```

---

### Task 7: BETWEEN Op

**Files:**
- Modify: `include/glaze_sql/condition.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを追加**

```cpp
TEST_CASE("condition: where_between") {
  using namespace glz_sql;
  auto c = where_between<"age">(int64_t{20}, int64_t{30});
  REQUIRE(c.fragment() == "age BETWEEN ? AND ?");
  REQUIRE(c.placeholder_count() == 2);
}
```

- [ ] **Step 2: ファクトリを追加**

```cpp
template <fixed_string C, typename V>
auto where_between(V lo, V hi) -> leaf_condition<compare_op::between, C, std::decay_t<V>, std::decay_t<V>> {
  return leaf_condition<compare_op::between, C, std::decay_t<V>, std::decay_t<V>>(std::move(lo), std::move(hi));
}
```

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/condition.hpp test/test_sql_repository.cpp
git commit -m "feat(condition): add where_between factory"
```

---

### Task 8: IS NULL / IS NOT NULL + any_condition コンセプト

**Files:**
- Modify: `include/glaze_sql/condition.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを追加**

```cpp
TEST_CASE("condition: is_null / is_not_null") {
  using namespace glz_sql;
  REQUIRE((where_is_null<"email">().fragment()) == "email IS NULL");
  REQUIRE(where_is_null<"email">().placeholder_count() == 0);
  REQUIRE((where_is_not_null<"email">().fragment()) == "email IS NOT NULL");
  REQUIRE(where_is_not_null<"email">().placeholder_count() == 0);
}
```

- [ ] **Step 2: ファクトリを追加**

`where_eq` の前 (ファクトリの直前) に以下を追加:

```cpp
/**
 * @brief IS NULL
 */
template <fixed_string C>
auto where_is_null() -> leaf_condition<compare_op::is_null, C> {
  return leaf_condition<compare_op::is_null, C>();
}

/**
 * @brief IS NOT NULL
 */
template <fixed_string C>
auto where_is_not_null() -> leaf_condition<compare_op::is_not_null, C> {
  return leaf_condition<compare_op::is_not_null, C>();
}
```

注: `is_leaf_condition_tag` は Task 3 で既に `leaf_condition` に追加済み。`any_condition` コンセプトは Task 2 で既にタグベースで正しく定義済み。

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/condition.hpp test/test_sql_repository.cpp
git commit -m "feat(condition): add where_is_null / where_is_not_null and any_condition concept"
```

---

### Task 9: composite_condition と operator&&

**Files:**
- Modify: `include/glaze_sql/condition.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを追加**

```cpp
TEST_CASE("condition: AND composition") {
  using namespace glz_sql;
  auto c = where_gt<"age">(int64_t{18}) && where_eq<"name">(std::string{"Alice"});
  REQUIRE(c.fragment() == "(age > ?) AND (name = ?)");
  REQUIRE(c.placeholder_count() == 2);
}
```

- [ ] **Step 2: composite_condition クラスと operator&& を追加**

`composite_condition` の前方宣言の直後に追加:

```cpp
/**
 * @brief 合成条件 (AND / OR)
 */
template <compare_op Op, typename L, typename R>
  requires (Op == compare_op::and_op || Op == compare_op::or_op)
class composite_condition {
  L left_;
  R right_;

public:
  using is_composite_condition_tag = void;
  using left_type = L;
  using right_type = R;

  constexpr composite_condition(L l, R r) : left_(std::move(l)), right_(std::move(r)) {}

  /**
   * @brief プレースホルダ数 (左右の葉の合計)
   */
  static constexpr auto placeholder_count() -> size_t {
    return L::placeholder_count() + R::placeholder_count();
  }

  /**
   * @brief SQL フラグメント
   */
  auto fragment() const -> std::string {
    if constexpr (Op == compare_op::and_op) {
      return std::format("({}) AND ({})", left_.fragment(), right_.fragment());
    } else {
      return std::format("({}) OR ({})", left_.fragment(), right_.fragment());
    }
  }

  /**
   * @brief バインド
   */
  auto bind(sqlite3_stmt* stmt, int start_index) const -> int {
    int idx = left_.bind(stmt, start_index);
    return right_.bind(stmt, idx);
  }
};

/**
 * @brief AND 演算子
 */
template <compare_op LOp, fixed_string LC, typename LV, typename LV2,
          compare_op ROp, fixed_string RC, typename RV, typename RV2>
auto operator&&(const leaf_condition<LOp, LC, LV, LV2>& l,
                const leaf_condition<ROp, RC, RV, RV2>& r)
    -> composite_condition<compare_op::and_op,
                           leaf_condition<LOp, LC, LV, LV2>,
                           leaf_condition<ROp, RC, RV, RV2>> {
  return composite_condition<compare_op::and_op,
                             leaf_condition<LOp, LC, LV, LV2>,
                             leaf_condition<ROp, RC, RV, RV2>>(l, r);
}
```

注: 葉条件同士の AND のみ提供。葉と合成の AND は Task 10 で追加する。

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/condition.hpp test/test_sql_repository.cpp
git commit -m "feat(condition): add composite_condition and operator&&"
```

---

### Task 10: operator&& の拡張 (leaf+composite, composite+leaf, composite+composite)

**Files:**
- Modify: `include/glaze_sql/condition.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを追加**

```cpp
TEST_CASE("condition: nested AND composition") {
  using namespace glz_sql;
  // (a && b) && c
  auto c = (where_gt<"age">(int64_t{18}) && where_eq<"name">(std::string{"Alice"})) && where_ne<"id">(int64_t{0});
  REQUIRE(c.placeholder_count() == 3);
  REQUIRE(c.fragment() == "((age > ?) AND (name = ?)) AND (id != ?)");
}
```

- [ ] **Step 2: operator&& のオーバーロードを追加**

`condition.hpp` の既存の `operator&&` の直後に追加:

```cpp
/**
 * @brief AND 演算子 (composite と leaf)
 */
template <compare_op LOp, typename LL, typename LR,
          compare_op ROp, fixed_string RC, typename RV, typename RV2>
  requires (LOp == compare_op::and_op || LOp == compare_op::or_op)
auto operator&&(const composite_condition<LOp, LL, LR>& l,
                const leaf_condition<ROp, RC, RV, RV2>& r)
    -> composite_condition<compare_op::and_op,
                           composite_condition<LOp, LL, LR>,
                           leaf_condition<ROp, RC, RV, RV2>> {
  return composite_condition<compare_op::and_op,
                             composite_condition<LOp, LL, LR>,
                             leaf_condition<ROp, RC, RV, RV2>>(l, r);
}

template <compare_op LOp, fixed_string LC, typename LV, typename LV2,
          compare_op ROp, typename RL, typename RR>
  requires (ROp == compare_op::and_op || ROp == compare_op::or_op)
auto operator&&(const leaf_condition<LOp, LC, LV, LV2>& l,
                const composite_condition<ROp, RL, RR>& r)
    -> composite_condition<compare_op::and_op,
                           leaf_condition<LOp, LC, LV, LV2>,
                           composite_condition<ROp, RL, RR>> {
  return composite_condition<compare_op::and_op,
                             leaf_condition<LOp, LC, LV, LV2>,
                             composite_condition<ROp, RL, RR>>(l, r);
}

/**
 * @brief AND 演算子 (composite と composite)
 */
template <compare_op LOp, typename LL, typename LR,
          compare_op ROp, typename RL, typename RR>
  requires ((LOp == compare_op::and_op || LOp == compare_op::or_op) &&
            (ROp == compare_op::and_op || ROp == compare_op::or_op))
auto operator&&(const composite_condition<LOp, LL, LR>& l,
                const composite_condition<ROp, RL, RR>& r)
    -> composite_condition<compare_op::and_op,
                           composite_condition<LOp, LL, LR>,
                           composite_condition<ROp, RL, RR>> {
  return composite_condition<compare_op::and_op,
                             composite_condition<LOp, LL, LR>,
                             composite_condition<ROp, RL, RR>>(l, r);
}
```

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/condition.hpp test/test_sql_repository.cpp
git commit -m "feat(condition): extend operator&& to support composite operands"
```

---

### Task 11: operator|| (OR)

**Files:**
- Modify: `include/glaze_sql/condition.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを追加**

```cpp
TEST_CASE("condition: OR composition") {
  using namespace glz_sql;
  auto c = where_lt<"age">(int64_t{10}) || where_gt<"age">(int64_t{60});
  REQUIRE(c.fragment() == "(age < ?) OR (age > ?)");
  REQUIRE(c.placeholder_count() == 2);
}

TEST_CASE("condition: AND and OR precedence") {
  using namespace glz_sql;
  // C++ の優先順位: && が || より強い → SQL でも同じ
  auto c = where_gt<"age">(int64_t{18}) && where_eq<"name">(std::string{"Alice"})
        || where_eq<"name">(std::string{"Bob"});
  REQUIRE(c.fragment() == "((age > ?) AND (name = ?)) OR (name = ?)");
}
```

- [ ] **Step 2: operator|| の 3 つのオーバーロードを追加**

`operator&&` のすべてのオーバーロードの後に `operator||` を追加 (テンプレートパラメータと `Op` の値を `and_op` → `or_op` に置換しただけの構造):

```cpp
/**
 * @brief OR 演算子 (leaf と leaf)
 */
template <compare_op LOp, fixed_string LC, typename LV, typename LV2,
          compare_op ROp, fixed_string RC, typename RV, typename RV2>
auto operator||(const leaf_condition<LOp, LC, LV, LV2>& l,
                const leaf_condition<ROp, RC, RV, RV2>& r)
    -> composite_condition<compare_op::or_op,
                           leaf_condition<LOp, LC, LV, LV2>,
                           leaf_condition<ROp, RC, RV, RV2>> {
  return composite_condition<compare_op::or_op,
                             leaf_condition<LOp, LC, LV, LV2>,
                             leaf_condition<ROp, RC, RV, RV2>>(l, r);
}

// composite+leaf, leaf+composite, composite+composite の 3 オーバーロード
// パターン自体は operator&& と同じ。Op を and_op → or_op、関数名 && → || に置換。
```

実装は `operator&&` と同じパターン。コードは spec を参照してそのまま転記する。

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/condition.hpp test/test_sql_repository.cpp
git commit -m "feat(condition): add operator|| with composite overloads"
```

---

### Task 12: valid_condition コンセプト

**Files:**
- Modify: `include/glaze_sql/condition.hpp`

- [ ] **Step 1: コンセプトを追加**

`any_condition` コンセプトの直後に追加:

```cpp
/**
 * @brief 葉条件のとき、そのカラム名が T に存在することを要求する
 */
template <typename Cond, typename T>
concept leaf_valid_for = any_condition<Cond> && requires {
  // 葉条件の葉 Op (eq..is_not_null) のみ
  requires is_leaf_op(Cond::op);
  { T::table_name };  // T は sql_table コンセプトを満たす
  requires valid_column<typename Cond::column_type, T>;
};

/**
 * @brief 合成条件のとき、左右の葉条件が T に対して妥当であることを要求する
 */
template <typename Cond, typename T>
concept composite_valid_for = any_condition<Cond> && requires {
  typename Cond::left_type;
  typename Cond::right_type;
  requires leaf_valid_for<typename Cond::left_type, T>
        || composite_valid_for<typename Cond::left_type, T>;
  requires leaf_valid_for<typename Cond::right_type, T>
        || composite_valid_for<typename Cond::right_type, T>;
};

/**
 * @brief Cond が T のカラムに対する妥当な条件式であることを保証する
 */
template <typename Cond, typename T>
concept valid_condition =
    leaf_valid_for<Cond, T> || composite_valid_for<Cond, T>;
```

注: `leaf_condition` には `static constexpr compare_op op = Op;` / `using column_type = decltype(C);` が `composite_condition` には `using left_type = L;` / `using right_type = R;` が Task 3, 9 で既に追加済み。

- [ ] **Step 2: (スキップ) 型エイリアスは Task 3, 9 で既に追加済み**

- [ ] **Step 2: テストで確認**

`test_sql_repository.cpp` に追加:

```cpp
TEST_CASE("condition: valid_condition concept") {
  using namespace glz_sql;
  static_assert(valid_condition<decltype(where_eq<"name">(std::string{"x"})), User>);
  static_assert(!valid_condition<decltype(where_eq<"invalid">(std::string{"x"})), User>);
  SUCCEED("valid_condition correctly checks column names");
}
```

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 3: コミット**

```bash
git add include/glaze_sql/condition.hpp test/test_sql_repository.cpp
git commit -m "feat(condition): add valid_condition concept with type aliases"
```

---

### Task 13: sql_repository の select_by を新 API に置換

**Files:**
- Modify: `include/glaze_sql/sql_repository.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを移行**

既存テスト `TEST_CASE("sql_repository: select_by")` を以下に置換:

```cpp
TEST_CASE("sql_repository: select_by single condition") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});
  repo.insert(User{.id = 3, .name = "Alice", .score = 92.0});

  auto alices = repo.select_by(glz_sql::where_eq<"name">(std::string{"Alice"}));
  REQUIRE(alices.size() == 2);
}
```

- [ ] **Step 2: ビルドして失敗を確認**

Run: `./build.sh`
Expected: コンパイル失敗 (select_by が新シグネチャでない)

- [ ] **Step 3: sql_repository.hpp に condition.hpp を include**

`sql_repository.hpp` の先頭に追加:

```cpp
#include "condition.hpp"
```

- [ ] **Step 4: select_by のシグネチャを置換**

`sql_repository.hpp` の `select_by` を以下に置換:

```cpp
  /**
   * @brief 条件式で検索する（複数件）
   * @param cond 条件式 (where_* の合成)
   * @return レコードのベクタ
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto select_by(const Cond& cond) const -> std::vector<T> {
    auto const sql  = std::format("SELECT {} FROM {} WHERE {};",
                                  join_field_names(), T::table_name, cond.fragment());
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare select_by: " << db_.error_message() << std::endl;
      return {};
    }
    cond.bind(stmt.get(), 1);
    return fetch_all(stmt.get());
  }
```

- [ ] **Step 5: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 6: コミット**

```bash
git add include/glaze_sql/sql_repository.hpp test/test_sql_repository.cpp
git commit -m "feat(repository): replace select_by with condition-based API"
```

---

### Task 14: sql_repository の find_by を新 API に置換

**Files:**
- Modify: `include/glaze_sql/sql_repository.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを移行**

既存テスト `TEST_CASE("sql_repository: find_by")` を以下に置換:

```cpp
TEST_CASE("sql_repository: find_by single condition") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  auto alice = repo.find_by(glz_sql::where_eq<"name">(std::string{"Alice"}));
  REQUIRE(alice.has_value());
  REQUIRE(alice->id == 1);

  auto not_found = repo.find_by(glz_sql::where_eq<"name">(std::string{"Charlie"}));
  REQUIRE_FALSE(not_found.has_value());
}
```

- [ ] **Step 2: find_by のシグネチャを置換**

`sql_repository.hpp` の `find_by` を以下に置換:

```cpp
  /**
   * @brief 条件式で 1 件検索する
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto find_by(const Cond& cond) const -> std::optional<T> {
    auto const sql  = std::format("SELECT {} FROM {} WHERE {};",
                                  join_field_names(), T::table_name, cond.fragment());
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare find_by: " << db_.error_message() << std::endl;
      return std::nullopt;
    }
    cond.bind(stmt.get(), 1);
    auto const result = sqlite3_step(stmt.get());
    if (result != SQLITE_ROW) {
      return std::nullopt;
    }
    return fetch_one(stmt.get());
  }
```

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/sql_repository.hpp test/test_sql_repository.cpp
git commit -m "feat(repository): replace find_by with condition-based API"
```

---

### Task 15: sql_repository の update_by を新 API に置換

**Files:**
- Modify: `include/glaze_sql/sql_repository.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを移行**

既存テスト `TEST_CASE("sql_repository: update_by")` および `"sql_repository: update_by by id column"` を以下に置換:

```cpp
TEST_CASE("sql_repository: update_by single condition") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});

  repo.update_by(User{.id = 1, .name = "Alice", .score = 100.0},
                 glz_sql::where_eq<"name">(std::string{"Alice"}));

  auto alice = repo.find_by(glz_sql::where_eq<"name">(std::string{"Alice"}));
  REQUIRE(alice.has_value());
  REQUIRE(alice->score == 100.0);
}

TEST_CASE("sql_repository: update_by by id column") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  repo.update_by(User{.id = 2, .name = "Bob", .score = 100.0},
                 glz_sql::where_eq<"id">(int64_t{2}));

  auto bob = repo.find_by(glz_sql::where_eq<"id">(int64_t{2}));
  REQUIRE(bob.has_value());
  REQUIRE(bob->score == 100.0);
}
```

- [ ] **Step 2: update_by のシグネチャを置換**

`sql_repository.hpp` の `update_by` を以下に置換:

```cpp
  /**
   * @brief 条件式でレコードを更新する
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto update_by(const T& record, const Cond& cond) const -> bool {
    auto const sql  = generate_update_sql(cond.fragment());
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare update_by: " << db_.error_message() << std::endl;
      return false;
    }
    bind_fields(stmt.get(), record);
    cond.bind(stmt.get(), static_cast<int>(field_count()) + 1);
    auto const result = sqlite3_step(stmt.get());
    if (result != SQLITE_DONE) {
      std::cerr << "ERROR: Failed to execute update_by: " << db_.error_message() << std::endl;
      return false;
    }
    return true;
  }
```

`generate_update_by_sql` を `generate_update_sql(cond_frag)` に置換し、SET 句と WHERE 句を別々に生成する形にリファクタ:

```cpp
  /**
   * @brief UPDATE 文の SET 句を生成
   */
  static auto generate_set_clause() -> std::string {
    std::string set_clause;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((set_clause += std::string(field_name_at<Is>()) + " = ?", set_clause += ","), ...);
    }(std::make_index_sequence<field_count()>{});
    if (!set_clause.empty()) {
      set_clause.pop_back();
    }
    return set_clause;
  }

  /**
   * @brief UPDATE 文を生成
   */
  static auto generate_update_sql(std::string_view cond_frag) -> std::string {
    return std::format("UPDATE {} SET {} WHERE {};",
                       T::table_name, generate_set_clause(), cond_frag);
  }
```

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/sql_repository.hpp test/test_sql_repository.cpp
git commit -m "feat(repository): replace update_by with condition-based API"
```

---

### Task 16: sql_repository の remove_by を新 API に置換

**Files:**
- Modify: `include/glaze_sql/sql_repository.hpp`
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを移行**

既存テスト `TEST_CASE("sql_repository: remove_by")` を以下に置換:

```cpp
TEST_CASE("sql_repository: remove_by single condition") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  repo.remove_by(glz_sql::where_eq<"name">(std::string{"Bob"}));

  auto all = repo.select_all();
  REQUIRE(all.size() == 1);
  REQUIRE(all[0].name == "Alice");
}
```

- [ ] **Step 2: remove_by のシグネチャを置換**

`sql_repository.hpp` の `remove_by` を以下に置換:

```cpp
  /**
   * @brief 条件式でレコードを削除する
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto remove_by(const Cond& cond) const -> bool {
    auto const sql  = std::format("DELETE FROM {} WHERE {};", T::table_name, cond.fragment());
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare remove_by: " << db_.error_message() << std::endl;
      return false;
    }
    cond.bind(stmt.get(), 1);
    auto const result = sqlite3_step(stmt.get());
    if (result != SQLITE_DONE) {
      std::cerr << "ERROR: Failed to execute remove_by: " << db_.error_message() << std::endl;
      return false;
    }
    return true;
  }
```

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/sql_repository.hpp test/test_sql_repository.cpp
git commit -m "feat(repository): replace remove_by with condition-based API"
```

---

### Task 17: 複数条件の統合テスト (AND / OR)

**Files:**
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを追加**

`test_sql_repository.cpp` の末尾に追加:

```cpp
TEST_CASE("sql_repository: select_by with AND") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Alice", .score = 80.0});
  repo.insert(User{.id = 3, .name = "Bob",   .score = 70.0});

  auto results = repo.select_by(
    glz_sql::where_eq<"name">(std::string{"Alice"}) && glz_sql::where_gt<"score">(90.0)
  );
  REQUIRE(results.size() == 1);
  REQUIRE(results[0].id == 1);
}

TEST_CASE("sql_repository: select_by with OR") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob",   .score = 87.3});
  repo.insert(User{.id = 3, .name = "Carol", .score = 70.0});

  auto results = repo.select_by(
    glz_sql::where_lt<"score">(80.0) || glz_sql::where_gt<"score">(90.0)
  );
  REQUIRE(results.size() == 2);
}

TEST_CASE("sql_repository: select_by with AND + OR precedence") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Alice", .score = 70.0});
  repo.insert(User{.id = 3, .name = "Bob",   .score = 95.0});

  // (Alice AND score>90) OR (Bob AND score>90)
  auto results = repo.select_by(
    (glz_sql::where_eq<"name">(std::string{"Alice"}) && glz_sql::where_gt<"score">(90.0))
    || (glz_sql::where_eq<"name">(std::string{"Bob"}) && glz_sql::where_gt<"score">(90.0))
  );
  REQUIRE(results.size() == 2);
}
```

- [ ] **Step 2: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 3: コミット**

```bash
git add test/test_sql_repository.cpp
git commit -m "test: add AND/OR multi-condition tests for select_by"
```

---

### Task 18: IN / BETWEEN / LIKE / IS NULL テスト

**Files:**
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: User に optional email フィールドを追加 (テスト用)**

`test_sql_repository.cpp` の User 構造体を以下に置換:

```cpp
struct User {
  int64_t                                id{};
  std::optional<std::string>             email;
  std::string                            name;
  int64_t                                age{};
  double                                 score{};
  static constexpr std::string_view      table_name = "users";
};

template <>
struct glz::meta<User> {
  using type                  = User;
  static constexpr auto value = object(
    "id", &User::id,
    "email", &User::email,
    "name", &User::name,
    "age", &User::age,
    "score", &User::score
  );
};
```

- [ ] **Step 2: テストを追加**

```cpp
TEST_CASE("sql_repository: select_by with IN") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "A", .age = 10, .score = 1.0});
  repo.insert(User{.id = 2, .name = "B", .age = 20, .score = 2.0});
  repo.insert(User{.id = 3, .name = "C", .age = 30, .score = 3.0});

  auto results = repo.select_by(glz_sql::where_in<"id">(int64_t{1}, int64_t{3}));
  REQUIRE(results.size() == 2);
}

TEST_CASE("sql_repository: select_by with BETWEEN") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "A", .age = 10, .score = 1.0});
  repo.insert(User{.id = 2, .name = "B", .age = 20, .score = 2.0});
  repo.insert(User{.id = 3, .name = "C", .age = 30, .score = 3.0});

  auto results = repo.select_by(glz_sql::where_between<"age">(int64_t{15}, int64_t{25}));
  REQUIRE(results.size() == 1);
  REQUIRE(results[0].id == 2);
}

TEST_CASE("sql_repository: select_by with LIKE") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 1.0});
  repo.insert(User{.id = 2, .name = "Bob",   .score = 2.0});
  repo.insert(User{.id = 3, .name = "Alex",  .score = 3.0});

  auto results = repo.select_by(glz_sql::where_like<"name">(std::string{"Al%"}));
  REQUIRE(results.size() == 2);
}

TEST_CASE("sql_repository: select_by with IS NULL") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "A", .email = std::string{"a@x"}, .score = 1.0});
  repo.insert(User{.id = 2, .name = "B", .email = std::nullopt,        .score = 2.0});

  auto no_email = repo.select_by(glz_sql::where_is_null<"email">());
  REQUIRE(no_email.size() == 1);
  REQUIRE(no_email[0].id == 2);

  auto has_email = repo.select_by(glz_sql::where_is_not_null<"email">());
  REQUIRE(has_email.size() == 1);
  REQUIRE(has_email[0].id == 1);
}
```

- [ ] **Step 3: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 4: コミット**

```bash
git add test/test_sql_repository.cpp
git commit -m "test: add IN/BETWEEN/LIKE/IS NULL tests"
```

---

### Task 19: update_by / remove_by の複数条件 + バインド順テスト

**Files:**
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを追加**

```cpp
TEST_CASE("sql_repository: update_by with multiple conditions") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .age = 20, .score = 1.0});
  repo.insert(User{.id = 2, .name = "Alice", .age = 30, .score = 2.0});
  repo.insert(User{.id = 3, .name = "Bob",   .age = 25, .score = 3.0});

  // name=Alice AND age<25 のみ更新
  repo.update_by(User{.id = 1, .name = "Alice", .age = 20, .score = 999.0},
                 glz_sql::where_eq<"name">(std::string{"Alice"}) && glz_sql::where_lt<"age">(int64_t{25}));

  auto a1 = repo.find_by(glz_sql::where_eq<"id">(int64_t{1}));
  REQUIRE(a1.has_value());
  REQUIRE(a1->score == 999.0);

  auto a2 = repo.find_by(glz_sql::where_eq<"id">(int64_t{2}));
  REQUIRE(a2.has_value());
  REQUIRE(a2->score == 2.0);  // 変更されていない
}

TEST_CASE("sql_repository: update_by bind order (SET then WHERE)") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .age = 20, .score = 1.0});
  repo.insert(User{.id = 2, .name = "Bob",   .age = 30, .score = 2.0});

  // ヒット対象は id=2 のみ。SET 句に id=99 を指定しても WHERE 句で id=2 に絞られる。
  // → バインド順が正しければ id=2 の score のみ更新される
  repo.update_by(User{.id = 99, .name = "Updated", .age = 99, .score = 100.0},
                 glz_sql::where_eq<"id">(int64_t{2}));

  auto updated = repo.find_by(glz_sql::where_eq<"id">(int64_t{2}));
  REQUIRE(updated.has_value());
  REQUIRE(updated->score == 100.0);

  // id=1 は影響を受けない
  auto unchanged = repo.find_by(glz_sql::where_eq<"id">(int64_t{1}));
  REQUIRE(unchanged.has_value());
  REQUIRE(unchanged->score == 1.0);
}

TEST_CASE("sql_repository: remove_by with multiple conditions") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .age = 20, .score = 1.0});
  repo.insert(User{.id = 2, .name = "Alice", .age = 30, .score = 2.0});
  repo.insert(User{.id = 3, .name = "Bob",   .age = 25, .score = 3.0});

  // name=Alice OR name=Bob
  repo.remove_by(glz_sql::where_eq<"name">(std::string{"Alice"})
                 || glz_sql::where_eq<"name">(std::string{"Bob"}));

  auto all = repo.select_all();
  REQUIRE(all.size() == 0);
}

TEST_CASE("sql_repository: update_by with IN") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "A", .score = 1.0});
  repo.insert(User{.id = 2, .name = "B", .score = 2.0});
  repo.insert(User{.id = 3, .name = "C", .score = 3.0});

  repo.update_by(User{.id = 1, .name = "A", .score = 0.0},
                 glz_sql::where_in<"id">(int64_t{1}, int64_t{3}));

  auto a1 = repo.find_by(glz_sql::where_eq<"id">(int64_t{1}));
  auto a3 = repo.find_by(glz_sql::where_eq<"id">(int64_t{3}));
  auto a2 = repo.find_by(glz_sql::where_eq<"id">(int64_t{2}));
  REQUIRE(a1->score == 0.0);
  REQUIRE(a3->score == 0.0);
  REQUIRE(a2->score == 2.0);  // 未更新
}
```

- [ ] **Step 2: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS

- [ ] **Step 3: コミット**

```bash
git add test/test_sql_repository.cpp
git commit -m "test: add update_by/remove_by multi-condition tests with bind order"
```

---

### Task 20: コンパイル時ネガティブテスト

**Files:**
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを追加**

```cpp
TEST_CASE("condition: compile-time invalid column") {
  using namespace glz_sql;
  // 以下の行のコメントを外すとコンパイルエラーになる
  // auto bad = where_eq<"invalid_column">(std::string{"x"});
  // static_assert(!valid_condition<decltype(bad), User>);

  static_assert(valid_condition<decltype(where_eq<"name">(std::string{"x"})), User>);
  static_assert(valid_condition<decltype(where_eq<"id">(int64_t{1})), User>);
  SUCCEED("compile-time column validation works");
}
```

- [ ] **Step 2: テストを実行**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: PASS (コメントアウトされたコードは評価されない)

- [ ] **Step 3: コミット**

```bash
git add test/test_sql_repository.cpp
git commit -m "test: add compile-time column validation tests"
```

---

### Task 21: 最終検証

- [ ] **Step 1: フルビルドを実行**

Run: `./build.sh`
Expected: 成功

- [ ] **Step 2: 全テストを実行**

Run: `cd build && ctest --output-on-failure`
Expected: 全テスト PASS

- [ ] **Step 3: clang-format を実行**

Run: `clang-format -i include/glaze_sql/*.hpp test/test_sql_repository.cpp`

- [ ] **Step 4: フォーマット後のビルド確認**

Run: `./build.sh && cd build && ctest --output-on-failure`
Expected: 全テスト PASS

- [ ] **Step 5: 最終コミット**

```bash
git add -A
git commit -m "chore: format code and final cleanup" --allow-empty
```

---

## 実装メモ

### 設計のポイント

- `leaf_condition<Op, C, V, V2>` の `Op` および `C` はテンプレートパラメータ (NTTP)。`C` はカラム名をコンパイル時に保持する。
- `composite_condition<Op, L, R>` は `&&` / `||` 演算子の戻り型。`fragment()` / `bind()` / `placeholder_count()` は再帰的に子に委譲する。
- `valid_condition<Cond, T>` コンセプトで全葉のカラム名を検証。`composite` の場合は `left_type` / `right_type` を再帰的にチェック。
- `update_by` は SET 句のフィールドバインドを 1..N に、WHRER 句の条件を N+1 以降に配置。`cond.bind(stmt, N+1)` で次位置を受け取り、再帰的に進める。

### 想定外のケース

- `where_eq<"name">("Alice")` の V は `const char(&)[6]` → decay で `const char*` になり、リテラルへのポインタを保持する。リテラルは静的ストレージなので安全。
- `where_eq<"id">(int64_t{2})` のように値型のコンストラクタを明示的に呼ぶと、V = int64_t になり decay 後も int64_t。値がコピーされて condition に格納される。
- 文字列リテラルを `where_eq` に直接渡す場合、内部的には `const char*` の decay として扱われる。生存期間はリテラルの静的ストレージで保証される。
