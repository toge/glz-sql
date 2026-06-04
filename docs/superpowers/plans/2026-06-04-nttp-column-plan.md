# NTTP化実装計画（fixed_string 版）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `find_by`, `select_by`, `update_by`, `remove_by` の条件カラム引数を NTTP 化し、コンパイル時にカラム名を検証する

**Architecture:** `fixed_string<N>` クラスを自作し、`std::string_view` の代替として NTTP として使用する。`glz::reflect<T>::keys` と比較してコンパイル時にカラム名を検証するコンセプトを追加する

**Tech Stack:** C++26, Glaze, SQLite3

---

## 変更概要

### fixed_string<N> クラス

```cpp
/**
 * @brief コンパイル時文字列を保持する構造体（NTTP として使用可能）
 */
template <size_t N>
struct fixed_string {
  char data[N]{};

  /**
   * @brief 文字列リテラルから構築するコンストラクタ
   */
  consteval fixed_string(const char (&str)[N]) {
    for (size_t i = 0; i < N; ++i) {
      data[i] = str[i];
    }
  }

  /**
   * @brief std::string_view に変換する
   */
  consteval operator std::string_view() const { return {data, N - 1}; }

  /**
   * @brief 文字列長を取得する
   */
  consteval auto size() const -> size_t { return N - 1; }
};
```

### メソッドシグネチャの変更

| メソッド | 現在のシグネチャ | NTTP化後のシグネチャ |
|---|---|---|
| `find_by` | `(std::string_view column, const V& value)` | `<fixed_string Column>(const V& value)` |
| `select_by` | `(std::string_view column, const V& value)` | `<fixed_string Column>(const V& value)` |
| `update_by` | `(const T& record, std::string_view cond_col, const V& cond_val)` | `<fixed_string CondCol>(const T& record, const V& cond_val)` |
| `remove_by` | `(std::string_view cond_col, const V& cond_val)` | `<fixed_string CondCol>(const V& cond_val)` |

### コンパイル時カラム名検証

```cpp
/**
 * @brief 指定されたカラム名が構造体のフィールドとして存在することを保証するコンセプト
 */
template <fixed_string Column, typename T>
concept valid_column = [] {
  constexpr auto n = glz::reflect<T>::size;
  for (size_t i = 0; i < n; ++i) {
    if (glz::reflect<T>::keys[i] == std::string_view(Column)) {
      return true;
    }
  }
  return false;
}();
```

### 使用例

```cpp
// NTTP化後の呼び出し方
repo.find_by<"name">("Alice");
repo.select_by<"name">("Alice");
repo.update_by<"name">(User{...}, "Alice");
repo.remove_by<"name">("Bob");

// 存在しないカラム名 → コンパイルエラー
repo.find_by<"invalid_column">("Alice");  // static_assert エラー
```

---

## タスク定義

### Task 1: fixed_string クラスの追加

**Files:**
- Modify: `include/glaze_sql/sql_repository.hpp`

- [ ] **Step 1: fixed_string クラスを追加**

`glz_sql` 名前空間の先頭に以下を追加:

```cpp
/**
 * @brief コンパイル時文字列を保持する構造体（NTTP として使用可能）
 */
template <size_t N>
struct fixed_string {
  char data[N]{};

  /**
   * @brief 文字列リテラルから構築するコンストラクタ
   */
  consteval fixed_string(const char (&str)[N]) {
    for (size_t i = 0; i < N; ++i) {
      data[i] = str[i];
    }
  }

  /**
   * @brief std::string_view に変換する
   */
  consteval operator std::string_view() const { return {data, N - 1}; }

  /**
   * @brief 文字列長を取得する
   */
  consteval auto size() const -> size_t { return N - 1; }
};
```

- [ ] **Step 2: valid_column コンセプトを追加**

`sql_table` コンセプトの後に以下を追加:

```cpp
/**
 * @brief 指定されたカラム名が構造体のフィールドとして存在することを保証するコンセプト
 */
template <fixed_string Column, typename T>
concept valid_column = [] {
  constexpr auto n = glz::reflect<T>::size;
  for (size_t i = 0; i < n; ++i) {
    if (glz::reflect<T>::keys[i] == std::string_view(Column)) {
      return true;
    }
  }
  return false;
}();
```

- [ ] **Step 3: ビルド確認**

```bash
./build.sh
```

- [ ] **Step 4: コミット**

```bash
git add include/glaze_sql/sql_repository.hpp
git commit -m "feat: add fixed_string and valid_column concept for NTTP column validation"
```

---

### Task 2: find_by の NTTP 化

**Files:**
- Modify: `include/glaze_sql/sql_repository.hpp`

- [ ] **Step 1: find_by のシグネチャを変更**

```cpp
/**
 * @brief 指定カラムで条件検索する（1件）
 * @tparam Column 条件カラム名（コンパイル時定数）
 * @param value 条件値
 * @return レコード（見つからない場合は std::nullopt）
 */
template <fixed_string Column, typename V>
  requires valid_column<Column, T>
auto find_by(const V& value) const -> std::optional<T> {
  auto const sql  = generate_select_by_sql(std::string_view(Column));
  auto       stmt = db_.prepare(sql);
  if (stmt == nullptr) {
    std::cerr << "ERROR: Failed to prepare find_by: " << db_.error_message() << std::endl;
    return std::nullopt;
  }
  bind_condition(stmt.get(), 1, value);
  auto const result = sqlite3_step(stmt.get());
  if (result != SQLITE_ROW) {
    return std::nullopt;
  }
  return fetch_one(stmt.get());
}
```

- [ ] **Step 2: ビルド確認**

```bash
./build.sh
```

- [ ] **Step 3: コミット**

```bash
git add include/glaze_sql/sql_repository.hpp
git commit -m "feat: NTTP-ify find_by with fixed_string"
```

---

### Task 3: select_by の NTTP 化

**Files:**
- Modify: `include/glaze_sql/sql_repository.hpp`

- [ ] **Step 1: select_by のシグネチャを変更**

```cpp
/**
 * @brief 指定カラムで条件検索する（複数件）
 * @tparam Column 条件カラム名（コンパイル時定数）
 * @param value 条件値
 * @return レコードのベクタ（0件の場合は空ベクタ）
 */
template <fixed_string Column, typename V>
  requires valid_column<Column, T>
auto select_by(const V& value) const -> std::vector<T> {
  auto const sql  = generate_select_by_sql(std::string_view(Column));
  auto       stmt = db_.prepare(sql);
  if (stmt == nullptr) {
    std::cerr << "ERROR: Failed to prepare select_by: " << db_.error_message() << std::endl;
    return {};
  }
  bind_condition(stmt.get(), 1, value);
  return fetch_all(stmt.get());
}
```

- [ ] **Step 2: ビルド確認**

```bash
./build.sh
```

- [ ] **Step 3: コミット**

```bash
git add include/glaze_sql/sql_repository.hpp
git commit -m "feat: NTTP-ify select_by with fixed_string"
```

---

### Task 4: update_by の NTTP 化

**Files:**
- Modify: `include/glaze_sql/sql_repository.hpp`

- [ ] **Step 1: update_by のシグネチャを変更**

```cpp
/**
 * @brief 指定カラムを条件に更新する
 * @tparam CondCol 条件カラム名（コンパイル時定数）
 * @param record 更新後のレコード
 * @param cond_val 条件値
 * @return 成功 true / 失敗 false
 */
template <fixed_string CondCol, typename V>
  requires valid_column<CondCol, T>
auto update_by(const T& record, const V& cond_val) const -> bool {
  auto const sql  = generate_update_by_sql(std::string_view(CondCol));
  auto       stmt = db_.prepare(sql);
  if (stmt == nullptr) {
    std::cerr << "ERROR: Failed to prepare update_by: " << db_.error_message() << std::endl;
    return false;
  }
  bind_fields(stmt.get(), record);
  bind_condition(stmt.get(), static_cast<int>(field_count()) + 1, cond_val);
  auto const result = sqlite3_step(stmt.get());
  if (result != SQLITE_DONE) {
    std::cerr << "ERROR: Failed to execute update_by: " << db_.error_message() << std::endl;
    return false;
  }
  return true;
}
```

- [ ] **Step 2: ビルド確認**

```bash
./build.sh
```

- [ ] **Step 3: コミット**

```bash
git add include/glaze_sql/sql_repository.hpp
git commit -m "feat: NTTP-ify update_by with fixed_string"
```

---

### Task 5: remove_by の NTTP 化

**Files:**
- Modify: `include/glaze_sql/sql_repository.hpp`

- [ ] **Step 1: remove_by のシグネチャを変更**

```cpp
/**
 * @brief 指定カラムを条件に削除する
 * @tparam CondCol 条件カラム名（コンパイル時定数）
 * @param cond_val 条件値
 * @return 成功 true / 失敗 false
 */
template <fixed_string CondCol, typename V>
  requires valid_column<CondCol, T>
auto remove_by(const V& cond_val) const -> bool {
  auto const sql  = generate_remove_by_sql(std::string_view(CondCol));
  auto       stmt = db_.prepare(sql);
  if (stmt == nullptr) {
    std::cerr << "ERROR: Failed to prepare remove_by: " << db_.error_message() << std::endl;
    return false;
  }
  bind_condition(stmt.get(), 1, cond_val);
  auto const result = sqlite3_step(stmt.get());
  if (result != SQLITE_DONE) {
    std::cerr << "ERROR: Failed to execute remove_by: " << db_.error_message() << std::endl;
    return false;
  }
  return true;
}
```

- [ ] **Step 2: ビルド確認**

```bash
./build.sh
```

- [ ] **Step 3: コミット**

```bash
git add include/glaze_sql/sql_repository.hpp
git commit -m "feat: NTTP-ify remove_by with fixed_string"
```

---

### Task 6: テストの更新

**Files:**
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: テストを NTTP 化後のシグネチャに更新**

既存のテスト呼び出しをすべて更新:

```cpp
// 変更前
repo.find_by("name", "Alice");
repo.select_by("name", "Alice");
repo.update_by(User{...}, "name", "Alice");
repo.remove_by("name", "Bob");

// 変更後
repo.find_by<"name">("Alice");
repo.select_by<"name">("Alice");
repo.update_by<"name">(User{...}, "Alice");
repo.remove_by<"name">("Bob");
```

- [ ] **Step 2: コンパイル時検証のテストを追加**

```cpp
TEST_CASE("sql_repository: compile-time column validation") {
  glz_sql::sqlite_database db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  // 存在しないカラム名 → コンパイルエラー（コメントアウトして確認）
  // auto result = repo.find_by<"invalid_column">("test");
}
```

- [ ] **Step 3: ビルドしてテストが通ることを確認**

```bash
./build.sh && cd build && ctest --output-on-failure
```

- [ ] **Step 4: コミット**

```bash
git add test/test_sql_repository.cpp
git commit -m "test: update tests for NTTP-ified methods"
```

---

### Task 7: 最終検証

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
