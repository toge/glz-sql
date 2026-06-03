# SqlRepository<T> Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Glaze リフレクションを活用した汎用 SQLite リポジトリクラス `glz_sql::sql_repository<T>` を実装する

**Architecture:** 抽象データベースインターフェース `database_interface` と SQLite 実装 `sqlite_database` を分離し、`sql_repository<T>` はインターフェースに依存。`sqlite_type_traits<T>` で型マッピング、`glz::meta<T>` でコンパイル時リフレクションを使用

**Tech Stack:** C++26, SQLite3, Glaze, quill, CMake, vcpkg, Catch2

---

## File Structure

| File | Purpose |
|---|---|
| `vcpkg.json` | sqlite3, quill を追加 |
| `CMakeLists.txt` | find_package + リンク設定を追加 |
| `include/glaze_sql/sqlite_bind.hpp` | C++型↔SQLite型のテンプレート特化 |
| `include/glaze_sql/database.hpp` | 抽象インターフェース + SQLite実装 |
| `include/glaze_sql/sql_repository.hpp` | `sql_repository<T>` テンプレートクラス |
| `test/test_sql_repository.cpp` | 動作確認サンプル + Catch2 テスト |

---

### Task 1: ビルドシステムの更新

**Files:**
- Modify: `vcpkg.json`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: vcpkg.json に sqlite3 と quill を追加**

```json
{
  "dependencies": [
    "glaze",
    "sqlite3",
    "quill"
  ]
}
```

- [ ] **Step 2: CMakeLists.txt に find_package を追加**

`find_package(glaze REQUIRED CONFIG)` の後に以下を追加:

```cmake
find_package(SQLite3 REQUIRED)
find_package(quill CONFIG REQUIRED)
```

- [ ] **Step 3: テストのリンク設定を更新**

`test/CMakeLists.txt` の `target_link_libraries` を以下に変更:

```cmake
target_link_libraries(all_test PRIVATE
  Catch2::Catch2
  Catch2::Catch2WithMain
  glaze::glaze
  SQLite3::SQLite3
  quill::quill
)
```

- [ ] **Step 4: include ディレクトリを作成**

```bash
mkdir -p include/glaze_sql
```

- [ ] **Step 5: ビルドして依存関係が正しく解決されることを確認**

```bash
./build.sh
```

- [ ] **Step 6: コミット**

```bash
git add vcpkg.json CMakeLists.txt test/CMakeLists.txt
git commit -m "build: add sqlite3 and quill dependencies"
```

---

### Task 2: sqlite_bind.hpp — 型マッピング

**Files:**
- Create: `include/glaze_sql/sqlite_bind.hpp`

- [ ] **Step 1: sqlite_bind.hpp を作成**

```cpp
#pragma once

#include <sqlite3.h>
#include <string>
#include <type_traits>

namespace glz_sql {

/**
 * @brief C++型からSQLite型へのマッピングを定義するテンプレートtrait
 *
 * 各特化で以下を提供:
 * - sql_type: SQL文で使用する型名文字列
 * - bind: sqlite3_bind_xxx 関数ポインタ
 * - column: sqlite3_column_xxx 関数ポインタ
 */
template <typename T>
struct sqlite_type_traits {
  static_assert(sizeof(T) == 0, "Unsupported type for SQLite binding");
};

/**
 * @brief int64_t の特殊化: INTEGER 型
 */
template <>
struct sqlite_type_traits<int64_t> {
  static constexpr const char* sql_type = "INTEGER";

  static auto bind(sqlite3_stmt* stmt, int index, int64_t value) -> int {
    return sqlite3_bind_int64(stmt, index, value);
  }

  static auto column(sqlite3_stmt* stmt, int index) -> int64_t {
    return sqlite3_column_int64(stmt, index);
  }
};

/**
 * @brief double の特殊化: REAL 型
 */
template <>
struct sqlite_type_traits<double> {
  static constexpr const char* sql_type = "REAL";

  static auto bind(sqlite3_stmt* stmt, int index, double value) -> int {
    return sqlite3_bind_double(stmt, index, value);
  }

  static auto column(sqlite3_stmt* stmt, int index) -> double {
    return sqlite3_column_double(stmt, index);
  }
};

/**
 * @brief std::string の特殊化: TEXT 型
 *
 * SQLITE_TRANSIENT を使用して内部コピーを作成
 */
template <>
struct sqlite_type_traits<std::string> {
  static constexpr const char* sql_type = "TEXT";

  static auto bind(sqlite3_stmt* stmt, int index, const std::string& value) -> int {
    return sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
  }

  static auto column(sqlite3_stmt* stmt, int index) -> std::string {
    auto const* text = sqlite3_column_text(stmt, index);
    if (text == nullptr) {
      return {};
    }
    return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(sqlite3_column_bytes(stmt, index)));
  }
};

}  // namespace glz_sql
```

- [ ] **Step 2: ヘッダーが正しくインクルードできることを確認**

`test/test_sql_repository.cpp` に一時的なインクルードチェックを追加:

```cpp
#include "glaze_sql/sqlite_bind.hpp"
```

ビルドが通ることを確認:

```bash
./build.sh
```

- [ ] **Step 3: コミット**

```bash
git add include/glaze_sql/sqlite_bind.hpp
git commit -m "feat: add sqlite_bind.hpp for C++/SQLite type mapping"
```

---

### Task 3: database.hpp — 抽象インターフェース + SQLite 実装

**Files:**
- Create: `include/glaze_sql/database.hpp`

- [ ] **Step 1: database.hpp を作成**

```cpp
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <sqlite3.h>

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
  virtual auto prepare(std::string_view sql) -> std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> = 0;

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

/**
 * @brief SQLite3 の具象データベースクラス
 */
class sqlite_database : public database_interface {
public:
  /**
   * @brief コンストラクタ。SQLite3 データベースを開く
   * @param db_path データベースファイルパス（":memory:" でインメモリ）
   */
  explicit sqlite_database(std::string_view db_path = ":memory:") {
    auto const result = sqlite3_open(db_path.data(), &db_);
    if (result != SQLITE_OK) {
      error_msg_ = sqlite3_errmsg(db_);
    }
  }

  /**
   * @brief デストラクタ。データベース接続を閉じる
   */
  ~sqlite_database() override {
    close();
  }

  // ムーブのみ許可
  sqlite_database(sqlite_database&& other) noexcept
    : db_(other.db_)
    , error_msg_(std::move(other.error_msg_)) {
    other.db_ = nullptr;
  }

  auto operator=(sqlite_database&& other) noexcept -> sqlite_database& {
    if (this != &other) {
      close();
      db_ = other.db_;
      error_msg_ = std::move(other.error_msg_);
      other.db_ = nullptr;
    }
    return *this;
  }

  // コピー禁止
  sqlite_database(const sqlite_database&) = delete;
  auto operator=(const sqlite_database&) -> sqlite_database& = delete;

  /**
   * @brief SQL文を実行する
   */
  auto execute(std::string_view sql) -> bool override {
    auto* char_msg = nullptr;
    auto const result = sqlite3_exec(db_, sql.data(), nullptr, nullptr, &char_msg);
    if (result != SQLITE_OK) {
      if (char_msg != nullptr) {
        error_msg_ = char_msg;
        sqlite3_free(char_msg);
      }
      return false;
    }
    return true;
  }

  /**
   * @brief プリペアドステートメントを準備する
   */
  auto prepare(std::string_view sql) -> std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> override {
    sqlite3_stmt* stmt = nullptr;
    auto const result = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (result != SQLITE_OK) {
      error_msg_ = sqlite3_errmsg(db_);
      return {nullptr, sqlite3_finalize};
    }
    return {stmt, sqlite3_finalize};
  }

  /**
   * @brief データベース接続を閉じる
   */
  void close() override {
    if (db_ != nullptr) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
  }

  /**
   * @brief エラーメッセージを取得する
   */
  auto error_message() -> std::string_view override {
    return error_msg_;
  }

  /**
   * @brief 生の sqlite3 ポインタを取得する
   */
  auto handle() -> sqlite3* {
    return db_;
  }

private:
  sqlite3* db_ = nullptr;
  std::string error_msg_;
};

}  // namespace glz_sql
```

- [ ] **Step 2: ビルドしてコンパイルが通ることを確認**

```bash
./build.sh
```

- [ ] **Step 3: コミット**

```bash
git add include/glaze_sql/database.hpp
git commit -m "feat: add database.hpp with abstract interface and SQLite implementation"
```

---

### Task 4: sql_repository.hpp — コアテンプレートクラス

**Files:**
- Create: `include/glaze_sql/sql_repository.hpp`

- [ ] **Step 1: sql_repository.hpp を作成**

```cpp
#pragma once

#include "database.hpp"
#include "sqlite_bind.hpp"

#include <glaze/glaze.hpp>
#include <quill/Quill.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace glz_sql {

/**
 * @brief 構造体が SQL テーブルとして使用できることを保証するコンセプト
 */
template <typename T>
concept sql_table = requires {
  { T::table_name } -> std::convertible_to<std::string_view>;
  glz::meta<T>::value;
};

/**
 * @brief Glaze リフレクションを活用した汎用 SQLite リポジトリクラス
 *
 * 任意の構造体 T を受け取り、CRUD 操作を提供する。
 * テーブル名は T::table_name から自動取得する。
 */
template <typename T>
  requires sql_table<T>
class sql_repository {
public:
  /**
   * @brief コンストラクタ。データベース接続を受け取る
   * @param db データベースインターフェースへの参照
   */
  explicit sql_repository(database_interface& db)
    : db_(db) {}

  /**
   * @brief テーブルを作成する
   * @return 成功 true / 失敗 false
   */
  auto create_table() const -> bool {
    auto const sql = generate_create_table_sql();
    if (!db_.execute(sql)) {
      LOG_ERROR(quill::get_logger(), "Failed to create table: {}", db_.error_message());
      return false;
    }
    return true;
  }

  /**
   * @brief レコードを挿入する
   * @param record 挿入するレコード
   * @return 成功 true / 失敗 false
   */
  auto insert(const T& record) const -> bool {
    auto const sql = generate_insert_sql();
    auto stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      LOG_ERROR(quill::get_logger(), "Failed to prepare insert: {}", db_.error_message());
      return false;
    }
    bind_fields(stmt.get(), record);
    auto const result = sqlite3_step(stmt.get());
    if (result != SQLITE_DONE) {
      LOG_ERROR(quill::get_logger(), "Failed to execute insert: {}", db_.error_message());
      return false;
    }
    return true;
  }

  /**
   * @brief 全件検索する
   * @return レコードのベクタ（0件の場合は空ベクタ）
   */
  auto select_all() const -> std::vector<T> {
    auto const sql = generate_select_all_sql();
    auto stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      LOG_ERROR(quill::get_logger(), "Failed to prepare select_all: {}", db_.error_message());
      return {};
    }
    return fetch_all(stmt.get());
  }

  /**
   * @brief 指定カラムで条件検索する（複数件）
   * @param column 条件カラム名
   * @param value 条件値
   * @return レコードのベクタ（0件の場合は空ベクタ）
   */
  template <typename V>
  auto select_by(std::string_view column, const V& value) const -> std::vector<T> {
    auto const sql = generate_select_by_sql(column);
    auto stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      LOG_ERROR(quill::get_logger(), "Failed to prepare select_by: {}", db_.error_message());
      return {};
    }
    bind_condition(stmt.get(), 1, value);
    return fetch_all(stmt.get());
  }

  /**
   * @brief 指定カラムで条件検索する（1件）
   * @param column 条件カラム名
   * @param value 条件値
   * @return レコード（見つからない場合は std::nullopt）
   */
  template <typename V>
  auto find_by(std::string_view column, const V& value) const -> std::optional<T> {
    auto const sql = generate_select_by_sql(column);
    auto stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      LOG_ERROR(quill::get_logger(), "Failed to prepare find_by: {}", db_.error_message());
      return std::nullopt;
    }
    bind_condition(stmt.get(), 1, value);
    auto const result = sqlite3_step(stmt.get());
    if (result != SQLITE_ROW) {
      return std::nullopt;
    }
    return fetch_one(stmt.get());
  }

  /**
   * @brief 指定カラムを条件に更新する
   * @param record 更新後のレコード
   * @param cond_col 条件カラム名
   * @param cond_val 条件値
   * @return 成功 true / 失敗 false
   */
  template <typename V>
  auto update_by(const T& record, std::string_view cond_col, const V& cond_val) const -> bool {
    auto const sql = generate_update_by_sql(cond_col);
    auto stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      LOG_ERROR(quill::get_logger(), "Failed to prepare update_by: {}", db_.error_message());
      return false;
    }
    bind_fields(stmt.get(), record);
    bind_condition(stmt.get(), static_cast<int>(field_count()) + 1, cond_val);
    auto const result = sqlite3_step(stmt.get());
    if (result != SQLITE_DONE) {
      LOG_ERROR(quill::get_logger(), "Failed to execute update_by: {}", db_.error_message());
      return false;
    }
    return true;
  }

  /**
   * @brief 指定カラムを条件に削除する
   * @param cond_col 条件カラム名
   * @param cond_val 条件値
   * @return 成功 true / 失敗 false
   */
  template <typename V>
  auto remove_by(std::string_view cond_col, const V& cond_val) const -> bool {
    auto const sql = generate_remove_by_sql(cond_col);
    auto stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      LOG_ERROR(quill::get_logger(), "Failed to prepare remove_by: {}", db_.error_message());
      return false;
    }
    bind_condition(stmt.get(), 1, cond_val);
    auto const result = sqlite3_step(stmt.get());
    if (result != SQLITE_DONE) {
      LOG_ERROR(quill::get_logger(), "Failed to execute remove_by: {}", db_.error_message());
      return false;
    }
    return true;
  }

private:
  database_interface& db_;

  /**
   * @brief フィールド数を取得する
   */
  static constexpr auto field_count() -> size_t {
    return glz::tuple_size_v<decltype(glz::meta<T>::value)>;
  }

  /**
   * @brief 指定インデックスのフィールド名を取得する
   */
  template <size_t I>
  static constexpr auto field_name_at() -> std::string_view {
    return glz::get<I>(glz::meta<T>::value).first;
  }

  /**
   * @brief 指定インデックスのフィールド値を取得する
   */
  template <size_t I>
  static auto field_value_at(const T& t) -> decltype(auto) {
    return t.*(glz::get<I>(glz::meta<T>::value).second);
  }

  /**
   * @brief 指定インデックスの SQL 型名を取得する
   */
  template <size_t I>
  static constexpr auto field_sql_type() -> std::string_view {
    using field_type = std::remove_cvref_t<decltype(field_value_at<I>(std::declval<const T&>()))>;
    return sqlite_type_traits<field_type>::sql_type;
  }

  /**
   * @brief 全フィールドの名前をカンマ区切りで結合する
   */
  static auto join_field_names() -> std::string {
    std::string result;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((result += std::string(field_name_at<Is>()), result += ","), ...);
    }(std::make_index_sequence<field_count()>{});
    if (!result.empty()) {
      result.pop_back();  // 末尾のカンマを除去
    }
    return result;
  }

  /**
   * @brief プレースホルダー文字列を生成する（?, ?, ...）
   */
  static auto placeholders() -> std::string {
    std::string result;
    for (size_t i = 0; i < field_count(); ++i) {
      result += "?,";
    }
    if (!result.empty()) {
      result.pop_back();  // 末尾のカンマを除去
    }
    return result;
  }

  /**
   * @brief CREATE TABLE 文を生成する
   */
  static auto generate_create_table_sql() -> std::string {
    std::string columns;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((columns += std::string(field_name_at<Is>()) + " " + std::string(field_sql_type<Is>()), columns += ","), ...);
    }(std::make_index_sequence<field_count()>{});
    if (!columns.empty()) {
      columns.pop_back();
    }
    return std::format("CREATE TABLE IF NOT EXISTS {} ({});", T::table_name, columns);
  }

  /**
   * @brief INSERT 文を生成する
   */
  static auto generate_insert_sql() -> std::string {
    return std::format("INSERT INTO {} ({}) VALUES ({});", T::table_name, join_field_names(), placeholders());
  }

  /**
   * @brief SELECT ALL 文を生成する
   */
  static auto generate_select_all_sql() -> std::string {
    return std::format("SELECT {} FROM {};", join_field_names(), T::table_name);
  }

  /**
   * @brief SELECT WHERE 文を生成する
   */
  static auto generate_select_by_sql(std::string_view column) -> std::string {
    return std::format("SELECT {} FROM {} WHERE {} = ?;", join_field_names(), T::table_name, column);
  }

  /**
   * @brief UPDATE WHERE 文を生成する
   */
  static auto generate_update_by_sql(std::string_view cond_col) -> std::string {
    std::string set_clause;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((set_clause += std::string(field_name_at<Is>()) + " = ?", set_clause += ","), ...);
    }(std::make_index_sequence<field_count()>{});
    if (!set_clause.empty()) {
      set_clause.pop_back();
    }
    return std::format("UPDATE {} SET {} WHERE {} = ?;", T::table_name, set_clause, cond_col);
  }

  /**
   * @brief DELETE WHERE 文を生成する
   */
  static auto generate_remove_by_sql(std::string_view column) -> std::string {
    return std::format("DELETE FROM {} WHERE {} = ?;", T::table_name, column);
  }

  /**
   * @brief プリペアドステートメントにフィールド値をバインドする
   */
  static void bind_fields(sqlite3_stmt* stmt, const T& record) {
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((sqlite_type_traits<std::remove_cvref_t<decltype(field_value_at<Is>(record))>>::bind(
          stmt, static_cast<int>(Is + 1), field_value_at<Is>(record))),
       ...);
    }(std::make_index_sequence<field_count()>{});
  }

  /**
   * @brief 条件値をバインドする
   */
  template <typename V>
  static void bind_condition(sqlite3_stmt* stmt, int index, const V& value) {
    sqlite_type_traits<V>::bind(stmt, index, value);
  }

  /**
   * @brief ステートメントから全行を取得する
   */
  static auto fetch_all(sqlite3_stmt* stmt) -> std::vector<T> {
    std::vector<T> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      results.push_back(fetch_one(stmt));
    }
    return results;
  }

  /**
   * @brief ステートメントから現在の行を1件取得する
   */
  static auto fetch_one(sqlite3_stmt* stmt) -> T {
    T record{};
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((field_value_at<Is>(record) =
            sqlite_type_traits<std::remove_cvref_t<decltype(field_value_at<Is>(record))>>::column(
                stmt, static_cast<int>(Is))),
       ...);
    }(std::make_index_sequence<field_count()>{});
    return record;
  }
};

}  // namespace glz_sql
```

- [ ] **Step 2: ビルドしてコンパイルが通ることを確認**

```bash
./build.sh
```

- [ ] **Step 3: コミット**

```bash
git add include/glaze_sql/sql_repository.hpp
git commit -m "feat: add sql_repository.hpp with generic CRUD operations"
```

---

### Task 5: テスト・サンプルコード

**Files:**
- Create: `test/test_sql_repository.cpp`

- [ ] **Step 1: test_sql_repository.cpp を作成**

```cpp
#include "glaze_sql/database.hpp"
#include "glaze_sql/sql_repository.hpp"

#include "catch2/catch_all.hpp"
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

struct Product {
  int64_t id{};
  std::string name;
  double price{};
  static constexpr std::string_view table_name = "products";
};

template <> struct glz::meta<Product> {
  using type = Product;
  static constexpr auto value = object("id", &Product::id, "name", &Product::name, "price", &Product::price);
};

TEST_CASE("sql_repository: create_table") {
  glz_sql::sqlite_database db(":memory:");
  glz_sql::sql_repository<User> repo(db);

  REQUIRE(repo.create_table());
}

TEST_CASE("sql_repository: insert and select_all") {
  glz_sql::sqlite_database db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  auto all = repo.select_all();
  REQUIRE(all.size() == 2);
  REQUIRE(all[0].id == 1);
  REQUIRE(all[0].name == "Alice");
  REQUIRE(all[0].score == 95.5);
  REQUIRE(all[1].id == 2);
  REQUIRE(all[1].name == "Bob");
  REQUIRE(all[1].score == 87.3);
}

TEST_CASE("sql_repository: find_by") {
  glz_sql::sqlite_database db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  auto alice = repo.find_by("name", "Alice");
  REQUIRE(alice.has_value());
  REQUIRE(alice->id == 1);
  REQUIRE(alice->name == "Alice");
  REQUIRE(alice->score == 95.5);

  auto not_found = repo.find_by("name", "Charlie");
  REQUIRE_FALSE(not_found.has_value());
}

TEST_CASE("sql_repository: select_by") {
  glz_sql::sqlite_database db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});
  repo.insert(User{.id = 3, .name = "Alice", .score = 92.0});

  auto alices = repo.select_by("name", "Alice");
  REQUIRE(alices.size() == 2);
}

TEST_CASE("sql_repository: update_by") {
  glz_sql::sqlite_database db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});

  repo.update_by(User{.id = 1, .name = "Alice", .score = 100.0}, "name", "Alice");

  auto alice = repo.find_by("name", "Alice");
  REQUIRE(alice.has_value());
  REQUIRE(alice->score == 100.0);
}

TEST_CASE("sql_repository: remove_by") {
  glz_sql::sqlite_database db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  repo.remove_by("name", "Bob");

  auto all = repo.select_all();
  REQUIRE(all.size() == 1);
  REQUIRE(all[0].name == "Alice");
}

TEST_CASE("sql_repository: multiple tables") {
  glz_sql::sqlite_database db(":memory:");
  glz_sql::sql_repository<User> user_repo(db);
  glz_sql::sql_repository<Product> product_repo(db);

  user_repo.create_table();
  product_repo.create_table();

  user_repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  product_repo.insert(Product{.id = 1, .name = "Widget", .price = 9.99});

  auto users = user_repo.select_all();
  auto products = product_repo.select_all();

  REQUIRE(users.size() == 1);
  REQUIRE(products.size() == 1);
}
```

- [ ] **Step 2: ビルドしてテストが通ることを確認**

```bash
./build.sh && cd build && ctest --output-on-failure
```

- [ ] **Step 3: コミット**

```bash
git add test/test_sql_repository.cpp
git commit -m "test: add sql_repository tests with Catch2"
```

---

### Task 6: 最終検証

- [ ] **Step 1: フルビルドを実行**

```bash
./build.sh
```

- [ ] **Step 2: テストを実行**

```bash
cd build && ctest --output-on-failure
```

Expected: 全テスト PASS

- [ ] **Step 3: clang-format を実行**

```bash
clang-format -i include/glaze_sql/*.hpp test/test_sql_repository.cpp
```

- [ ] **Step 4: 最終コミット**

```bash
git add -A
git commit -m "chore: format code and final cleanup"
```
