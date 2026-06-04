# SqlRepository<T> Design Spec

## Overview

Glaze のリフレクション機能を活用し、任意の構造体（集成体）を SQLite のテーブル操作に対応させる汎用リポジトリクラス `glz_sql::sql_repository<T>` を実装する。将来のDB変更に備え、データベース操作を抽象インターフェースで分離する。

## Tech Stack

- C++26
- SQLite3 (vcpkg)
- Glaze (vcpkg)
- quill (vcpkg)
- CMake + vcpkg
- Catch2 (テスト)

## File Structure

```
include/
  glz-sql/
    sqlite_bind.hpp       -- C++型↔SQLite型のテンプレート特化
    database.hpp          -- 抽象データベースインターフェース + SQLite実装
    sql_repository.hpp    -- SqlRepository<T> テンプレートクラス
test/
  test_sql_repository.cpp -- 動作確認サンプル + テスト
```

## Dependency Changes

### vcpkg.json

```json
{
  "dependencies": [
    "glaze",
    "sqlite3",
    "quill"
  ]
}
```

### CMakeLists.txt

- `find_package(SQLite3 REQUIRED)` を追加
- `find_package(quill CONFIG REQUIRED)` を追加
- テストに `SQLite3::SQLite3`, `quill::quill` をリンク

## Namespace

```cpp
namespace glz_sql {
  // ...
}
```

## Database Abstraction: `database.hpp`

### Abstract Interface

将来のDB変更（PostgreSQL, MySQL等）に備え、データベース操作を抽象化する。

```cpp
namespace glz_sql {

/**
 * @brief データベース操作の抽象インターフェース
 *
 * 将来のDB変更時に、このインターフェースを実装するだけで対応可能
 */
class database_interface {
public:
  virtual ~database_interface() = default;

  /**
   * @brief SQL文を実行する
   * @param sql 実行するSQL文
   * @return 成功 true / 失敗 false
   */
  virtual auto execute(std::string_view sql) -> bool = 0;

  /**
   * @brief プリペアドステートメントを準備する
   * @param sql SQL文
   * @return ステートメント（RAII管理）
   */
  virtual auto prepare(std::string_view sql)
    -> std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> = 0;

  /**
   * @brief データベース接続を閉じる
   */
  virtual void close() = 0;

  /**
   * @brief エラーメッセージを取得する
   * @return エラーメッセージ
   */
  virtual auto error_message() -> std::string_view = 0;
};

}  // namespace glz_sql
```

### SQLite Implementation

```cpp
namespace glz_sql {

/**
 * @brief SQLite3 の具象データベースクラス
 */
class sqlite_database : public database_interface {
public:
  /**
   * @brief コンストラクタ。SQLite3 データベースを開く
   * @param db_path データベースファイルパス（":memory:" でインメモリ）
   */
  explicit sqlite_database(std::string_view db_path = ":memory:");

  /**
   * @brief デストラクタ。データベース接続を閉じる
   */
  ~sqlite_database() override;

  // ムーブのみ許可
  sqlite_database(sqlite_database&&) noexcept;
  auto operator=(sqlite_database&&) noexcept -> sqlite_database&;

  // コピー禁止
  sqlite_database(const sqlite_database&) = delete;
  auto operator=(const sqlite_database&) -> sqlite_database& = delete;

  auto execute(std::string_view sql) -> bool override;
  auto prepare(std::string_view sql)
    -> std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> override;
  void close() override;
  auto error_message() -> std::string_view override;

  /**
   * @brief 生の sqlite3 ポインタを取得する
   * @return sqlite3* ポインタ
   */
  auto handle() -> sqlite3*;

private:
  sqlite3* db_ = nullptr;
};

}  // namespace glz_sql
```

### 使用例

```cpp
// RAII でデータベース接続を管理
glz_sql::sqlite_database db(":memory:");

// または生のポインタを渡す場合
sqlite3* raw_db = nullptr;
sqlite3_open(":memory:", &raw_db);
glz_sql::sqlite_database db(raw_db);  // 所有権を移動
```

## Type Mapping: `sqlite_bind.hpp`

`sqlite_type_traits<T>` テンプレートtraitで C++型→SQLite型をマッピング。

| C++ Type | SQL Type | bind function | column function |
|---|---|---|---|
| `int64_t` | `INTEGER` | `sqlite3_bind_int64` | `sqlite3_column_int64` |
| `double` | `REAL` | `sqlite3_bind_double` | `sqlite3_column_double` |
| `std::string` | `TEXT` | `sqlite3_bind_text` (SQLITE_TRANSIENT) | `sqlite3_column_text` |

- 未サポート型は `static_assert` でコンパイルエラー
- `std::string` は `SQLITE_TRANSIENT` で内部コピー

## Core Class: `sql_repository<T>`

### Concept

```cpp
template <typename T>
concept sql_table = requires {
  { T::table_name } -> std::convertible_to<std::string_view>;
  glz::meta<T>::value;
};
```

### Class Interface

```cpp
namespace glz_sql {

template <typename T>
  requires sql_table<T>
class sql_repository {
public:
  /**
   * @brief コンストラクタ。データベース接続を受け取る
   * @param db データベースインターフェースへの参照
   */
  explicit sql_repository(database_interface& db);

  /**
   * @brief テーブルを作成する
   * @return 成功 true / 失敗 false
   */
  auto create_table() const -> bool;

  /**
   * @brief レコードを挿入する
   * @param record 挿入するレコード
   * @return 成功 true / 失敗 false
   */
  auto insert(const T& record) const -> bool;

  /**
   * @brief 全件検索する
   * @return レコードのベクタ（0件の場合は空ベクタ）
   */
  auto select_all() const -> std::vector<T>;

  /**
   * @brief 指定カラムで条件検索する（複数件）
   * @param column 条件カラム名
   * @param value 条件値
   * @return レコードのベクタ（0件の場合は空ベクタ）
   */
  template <typename V>
  auto select_by(std::string_view column, const V& value) const -> std::vector<T>;

  /**
   * @brief 指定カラムで条件検索する（1件）
   * @param column 条件カラム名
   * @param value 条件値
   * @return レコード（見つからない場合は std::nullopt）
   */
  template <typename V>
  auto find_by(std::string_view column, const V& value) const -> std::optional<T>;

  /**
   * @brief 指定カラムを条件に更新する
   * @param record 更新後のレコード
   * @param cond_col 条件カラム名
   * @param cond_val 条件値
   * @return 成功 true / 失敗 false
   */
  template <typename V>
  auto update_by(const T& record, std::string_view cond_col, const V& cond_val) const -> bool;

  /**
   * @brief 指定カラムを条件に削除する
   * @param cond_col 条件カラム名
   * @param cond_val 条件値
   * @return 成功 true / 失敗 false
   */
  template <typename V>
  auto remove_by(std::string_view cond_col, const V& cond_val) const -> bool;

private:
  database_interface& db_;

  /** @brief Glz リフレクションでフィールド名のリストを取得する */
  static constexpr auto field_names() -> /* 実装時に決定 */;

  /** @brief Glz リフレクションでフィールド値をバインドする */
  static void bind_fields(sqlite3_stmt* stmt, const T& record);
};

}  // namespace glz_sql
```

### Reflection Approach

`glz::meta<T>::value` の `glz::tuplet::tuple` を `std::apply` で分解し、コンパイル時にフィールド名・型・値を取得する。

### Glaze API の使い方

```cpp
// フィールド数を取得
constexpr auto n = glz::tuple_size_v<decltype(glz::meta<T>::value)>;

// std::index_sequence で各フィールドにアクセス
template <size_t I>
constexpr auto get_field_name() {
  return glz::get<I>(glz::meta<T>::value).first;  // std::string_view
}

// 各フィールドの値にアクセス
template <size_t I>
constexpr auto get_field_value(const T& t) {
  return t.*(glz::get<I>(glz::meta<T>::value).second);  // メンバポインタ経由
}
```

### 注意点

- Glaze のバージョンにより API が異なる可能性がある
- `glz::tuplet::tuple` は `std::apply` で分解可能
- フィールド名は `std::string_view` としてコンパイル時に取得可能

### SQL Generation

#### create_table

```
CREATE TABLE IF NOT EXISTS {table_name} ({field1} {sql_type1}, {field2} {sql_type2}, ...);
```

- 各フィールドの `sqlite_type_traits<T_field>::sql_type` から SQL 型名を取得
- `std::format` で文を組み立て

#### insert

```
INSERT INTO {table_name} ({field1}, {field2}, ...) VALUES (?, ?, ...);
```

- プレースホルダー数 = フィールド数
- `bind_fields()` で値をバインド

#### select_all

```
SELECT {field1}, {field2}, ... FROM {table_name};
```

#### select_by / find_by

```
SELECT {field1}, {field2}, ... FROM {table_name} WHERE {column} = ?;
```

- 条件値をバインド
- `sqlite3_step()` でループし各行を `T` に変換

#### update_by

```
UPDATE {table_name} SET {field1} = ?, {field2} = ?, ... WHERE {cond_col} = ?;
```

- SET 部に全フィールド、WHERE 部に条件カラム
- 条件値は最後にバインド

#### remove_by

```
DELETE FROM {table_name} WHERE {cond_col} = ?;
```

### Error Handling

- すべて `database_interface` の戻り値をチェック
- エラー時は `quill` で `LOG_ERROR` を出力
- 戻り値: `false` / `std::nullopt` / 空ベクタ

### Prepared Statement Management

- `std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>` で RAII 管理
- `database_interface::prepare()` でステートメントを準備

## Sample Code

```cpp
#include "glz-sql/sql_repository.hpp"
#include "glz-sql/database.hpp"
#include <quill/Quill.h>

struct User {
  int64_t id{};
  std::string name;
  double score{};
  static constexpr std::string_view table_name = "users";
};

template <> struct glz::meta<User> {
  using type = User;
  static constexpr auto value = object("id", &User::id, "name", &User::name, "score", &User::score);
};

auto main() -> int {
  // RAII でデータベース接続を管理
  glz_sql::sqlite_database db(":memory:");

  // リポジトリ作成
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  // データ挿入
  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  // 全件検索
  auto all = repo.select_all();

  // 条件検索
  auto alice = repo.find_by("name", "Alice");

  // 更新
  repo.update_by(User{.id = 1, .name = "Alice", .score = 100.0}, "name", "Alice");

  // 削除
  repo.remove_by("name", "Bob");

  // close() はデストラクタで自動呼び出し
}
```

## Coding Rules

- **言語**: C++26
- **コメント**: 日本語、Doxygen形式 (`@brief`, `@param`, `@return`)
- **インデント**: 半角スペース2文字
- **型推論**: `auto` を多用
- **定数**: `auto const` / `auto constexpr`
- **関数**: 可能な限り `const`, `constexpr`, `noexcept`
- **フォーマット**: `std::format` を使用
- **ロギング**: `quill` を使用
- **エラーハンドリング**: 例外を投げない（エラーコード + std::optional）
