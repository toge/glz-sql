# Iterator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement iterator-based select operations for glaze-sql to enable std::ranges processing.

**Architecture:** Add sql_iterator and sql_sentinel classes to manage sqlite3_stmt lifecycle and provide input iterator interface. Modify select_all/select_by to return iterator-sentinel pairs while maintaining backward compatibility with vector versions.

**Tech Stack:** C++26, SQLite3, Glaze reflection, std::ranges

---

## File Structure

**Modified files:**
- `include/glz-sql/sql_repository.hpp` - Add iterator/sentinel classes, modify select methods
- `test/test_sql_repository.cpp` - Add iterator tests, update existing tests

**New files:**
- None (all code added to existing files)

## Task Breakdown

### Task 1: Add sql_sentinel class

**Files:**
- Modify: `include/glz-sql/sql_repository.hpp:16-17` (inside namespace glz_sql)

- [ ] **Step 1: Write failing test for sql_sentinel**

Add to `test/test_sql_repository.cpp` after existing tests:

```cpp
TEST_CASE("sql_sentinel basic operations") {
    glz_sql::sql_sentinel sentinel;
    // sentinel is default constructible
    glz_sql::sql_sentinel sentinel2;
    // sentinel equality comparison
    REQUIRE(sentinel == sentinel2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build && ctest -R "sql_sentinel" -V`
Expected: FAIL with "use of undeclared identifier 'sql_sentinel'"

- [ ] **Step 3: Write minimal implementation**

Add to `include/glz-sql/sql_repository.hpp` before sql_repository class:

```cpp
/**
 * @brief イテレータの終端を表すセントリルクラス
 */
class sql_sentinel {
  public:
  sql_sentinel() = default;
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd build && ctest -R "sql_sentinel" -V`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/glz-sql/sql_repository.hpp test/test_sql_repository.cpp
git commit -m "feat: add sql_sentinel class for iterator end marker"
```

### Task 2: Add sql_iterator class skeleton

**Files:**
- Modify: `include/glz-sql/sql_repository.hpp:16-17` (inside namespace glz_sql, after sql_sentinel)

- [ ] **Step 1: Write failing test for sql_iterator**

Add to `test/test_sql_repository.cpp`:

```cpp
TEST_CASE("sql_iterator type traits") {
    using iterator = glz_sql::sql_iterator<User>;
    REQUIRE(std::input_iterator<iterator>);
    REQUIRE(std::sentinel_for<glz_sql::sql_sentinel, iterator>);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build && ctest -R "sql_iterator type traits" -V`
Expected: FAIL with "use of undeclared identifier 'sql_iterator'"

- [ ] **Step 3: Write minimal implementation**

Add to `include/glz-sql/sql_repository.hpp` after sql_sentinel:

```cpp
/**
 * @brief SQLite クエリ結果を走査する入力イテレータ
 */
template <typename T>
class sql_iterator {
  public:
  using value_type      = T;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::input_iterator_tag;
  using reference       = const T&;
  using pointer         = const T*;

  sql_iterator() = default;

  sql_iterator(database_interface& db, std::string_view sql) {
    auto stmt = db.prepare(sql);
    if (stmt == nullptr) {
      return;
    }
    stmt_ = std::move(stmt);
    advance();
  }

  template <typename Cond>
    requires valid_condition<Cond, T>
  sql_iterator(database_interface& db, std::string_view sql, const Cond& cond) {
    auto stmt = db.prepare(sql);
    if (stmt == nullptr) {
      return;
    }
    stmt_ = std::move(stmt);
    cond.bind(stmt_.get(), 1);
    advance();
  }

  sql_iterator(sql_iterator&&) = default;
  sql_iterator& operator=(sql_iterator&&) = default;

  sql_iterator(const sql_iterator&) = delete;
  sql_iterator& operator=(const sql_iterator&) = delete;

  [[nodiscard]] const T& operator*() const noexcept {
    return *current_;
  }

  sql_iterator& operator++() {
    advance();
    return *this;
  }

  void operator++(int) {
    ++*this;
  }

  [[nodiscard]] bool operator==(const sql_sentinel&) const noexcept {
    return !current_.has_value();
  }

  private:
  std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt_{nullptr, sqlite3_finalize};
  std::optional<T> current_;

  void advance() {
    if (stmt_ == nullptr) {
      current_.reset();
      return;
    }
    if (sqlite3_step(stmt_.get()) == SQLITE_ROW) {
      current_.emplace(fetch_one(stmt_.get()));
    }
    else {
      current_.reset();
    }
  }

  static auto fetch_one(sqlite3_stmt* stmt) -> T {
    T record{};
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((field_value_at_mutable<Is>(record) = sqlite_type_traits<std::remove_cvref_t<decltype(field_value_at<Is>(std::declval<const T&>()))>>::column(stmt, static_cast<int>(Is))), ...);
    }(std::make_index_sequence<field_count()>{});
    return record;
  }

  static constexpr auto field_count() -> size_t { return glz::reflect<T>::size; }

  template <size_t I>
  static auto field_value_at(const T& t) -> decltype(auto) {
    return t.*(glz::get<I>(glz::reflect<T>::values));
  }

  template <size_t I>
  static auto field_value_at_mutable(T& t) -> decltype(auto) {
    return t.*(glz::get<I>(glz::reflect<T>::values));
  }
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd build && ctest -R "sql_iterator type traits" -V`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/glz-sql/sql_repository.hpp test/test_sql_repository.cpp
git commit -m "feat: add sql_iterator class skeleton with type traits"
```

### Task 3: Implement select_all iterator version

**Files:**
- Modify: `include/glz-sql/sql_repository.hpp:95-103` (select_all method)

- [ ] **Step 1: Write failing test for select_all iterator**

Add to `test/test_sql_repository.cpp`:

```cpp
TEST_CASE("select_all iterator") {
    glz_sql::sqlite_database db(":memory:");
    glz_sql::sql_repository<User> repo(db);
    repo.create_table();

    User user1{.id = 1, .name = "Alice", .age = 25};
    User user2{.id = 2, .name = "Bob", .age = 30};
    repo.insert(user1);
    repo.insert(user2);

    auto [it, end] = repo.select_all();
    int count = 0;
    for (auto&& user : std::ranges::subrange(it, end)) {
        count++;
    }
    REQUIRE(count == 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build && ctest -R "select_all iterator" -V`
Expected: FAIL with "no member named 'select_all'"

- [ ] **Step 3: Add iterator-based select_all**

Rename existing select_all to select_all_vec and add new select_all:

```cpp
  /**
   * @brief 全件検索する（iterator版）
   * @return iteratorとsentinelのペア
   */
  auto select_all() const -> std::pair<sql_iterator<T>, sql_sentinel> {
    auto const sql = generate_select_all_sql();
    return {sql_iterator<T>(db_, sql), sql_sentinel{}};
  }

  /**
   * @brief 全件検索する（vector版）
   * @return レコードのベクタ（0件の場合は空ベクタ）
   */
  auto select_all_vec() const -> std::vector<T> {
    auto const sql  = generate_select_all_sql();
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare select_all: " << db_.error_message() << std::endl;
      return {};
    }
    return fetch_all(stmt.get());
  }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd build && ctest -R "select_all iterator" -V`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/glz-sql/sql_repository.hpp test/test_sql_repository.cpp
git commit -m "feat: add iterator-based select_all method"
```

### Task 4: Implement select_by iterator version

**Files:**
- Modify: `include/glz-sql/sql_repository.hpp:110-121` (select_by method)

- [ ] **Step 1: Write failing test for select_by iterator**

Add to `test/test_sql_repository.cpp`:

```cpp
TEST_CASE("select_by iterator") {
    glz_sql::sqlite_database db(":memory:");
    glz_sql::sql_repository<User> repo(db);
    repo.create_table();

    User user1{.id = 1, .name = "Alice", .age = 25};
    User user2{.id = 2, .name = "Bob", .age = 30};
    User user3{.id = 3, .name = "Charlie", .age = 25};
    repo.insert(user1);
    repo.insert(user2);
    repo.insert(user3);

    auto [it, end] = repo.select_by(where_eq<"age">(25));
    int count = 0;
    for (auto&& user : std::ranges::subrange(it, end)) {
        count++;
    }
    REQUIRE(count == 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build && ctest -R "select_by iterator" -V`
Expected: FAIL with "no member named 'select_by'"

- [ ] **Step 3: Add iterator-based select_by**

Rename existing select_by to select_by_vec and add new select_by:

```cpp
  /**
   * @brief 条件式で検索する（iterator版）
   * @param cond 条件式 (where_* の合成)
   * @return iteratorとsentinelのペア
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto select_by(const Cond& cond) const -> std::pair<sql_iterator<T>, sql_sentinel> {
    auto const sql = std::format("SELECT {} FROM {} WHERE {};", join_field_names(), T::table_name, cond.fragment());
    return {sql_iterator<T>(db_, sql, cond), sql_sentinel{}};
  }

  /**
   * @brief 条件式で検索する（vector版）
   * @param cond 条件式 (where_* の合成)
   * @return レコードのベクタ
   */
  template <typename Cond>
    requires valid_condition<Cond, T>
  auto select_by_vec(const Cond& cond) const -> std::vector<T> {
    auto const sql  = std::format("SELECT {} FROM {} WHERE {};", join_field_names(), T::table_name, cond.fragment());
    auto       stmt = db_.prepare(sql);
    if (stmt == nullptr) {
      std::cerr << "ERROR: Failed to prepare select_by: " << db_.error_message() << std::endl;
      return {};
    }
    cond.bind(stmt.get(), 1);
    return fetch_all(stmt.get());
  }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd build && ctest -R "select_by iterator" -V`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/glz-sql/sql_repository.hpp test/test_sql_repository.cpp
git commit -m "feat: add iterator-based select_by method"
```

### Task 5: Update existing tests for vector versions

**Files:**
- Modify: `test/test_sql_repository.cpp` (update existing tests)

- [ ] **Step 1: Update existing tests to use vector versions**

Find and replace in existing tests:
- `repo.select_all()` → `repo.select_all_vec()`
- `repo.select_by(...)` → `repo.select_by_vec(...)`

- [ ] **Step 2: Run all tests to verify they pass**

Run: `cd build && ctest -V`
Expected: All tests PASS

- [ ] **Step 3: Commit**

```bash
git add test/test_sql_repository.cpp
git commit -m "test: update existing tests to use vector versions"
```

### Task 6: Add std::ranges integration tests

**Files:**
- Modify: `test/test_sql_repository.cpp`

- [ ] **Step 1: Write std::ranges tests**

Add to `test/test_sql_repository.cpp`:

```cpp
TEST_CASE("std::ranges::for_each with select_all") {
    glz_sql::sqlite_database db(":memory:");
    glz_sql::sql_repository<User> repo(db);
    repo.create_table();

    User user1{.id = 1, .name = "Alice", .age = 25};
    User user2{.id = 2, .name = "Bob", .age = 30};
    repo.insert(user1);
    repo.insert(user2);

    auto [it, end] = repo.select_all();
    std::vector<std::string> names;
    std::ranges::for_each(std::ranges::subrange(it, end), [&](const User& u) {
        names.push_back(u.name);
    });
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "Alice");
    REQUIRE(names[1] == "Bob");
}

TEST_CASE("std::ranges::filter_view with select_all") {
    glz_sql::sqlite_database db(":memory:");
    glz_sql::sql_repository<User> repo(db);
    repo.create_table();

    User user1{.id = 1, .name = "Alice", .age = 25};
    User user2{.id = 2, .name = "Bob", .age = 30};
    User user3{.id = 3, .name = "Charlie", .age = 35};
    repo.insert(user1);
    repo.insert(user2);
    repo.insert(user3);

    auto [it, end] = repo.select_all();
    auto filtered = std::ranges::filter_view(
        std::ranges::subrange(it, end),
        [](const User& u) { return u.age > 25; });
    int count = 0;
    for (auto&& user : filtered) {
        count++;
    }
    REQUIRE(count == 2);
}

TEST_CASE("std::ranges::transform_view") {
    glz_sql::sqlite_database db(":memory:");
    glz_sql::sql_repository<User> repo(db);
    repo.create_table();

    User user1{.id = 1, .name = "Alice", .age = 25};
    User user2{.id = 2, .name = "Bob", .age = 30};
    repo.insert(user1);
    repo.insert(user2);

    auto [it, end] = repo.select_all();
    auto transformed = std::ranges::transform_view(
        std::ranges::subrange(it, end),
        [](const User& u) { return u.name; });
    std::vector<std::string> names(transformed.begin(), transformed.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "Alice");
    REQUIRE(names[1] == "Bob");
}
```

- [ ] **Step 2: Run tests to verify they pass**

Run: `cd build && ctest -V`
Expected: All tests PASS

- [ ] **Step 3: Commit**

```bash
git add test/test_sql_repository.cpp
git commit -m "test: add std::ranges integration tests"
```

## Self-Review

1. **Spec coverage:** All requirements covered - iterator/sentinel classes, select_all/select_by changes, vector versions maintained, std::ranges integration.
2. **Placeholder scan:** No placeholders found - all steps have concrete code and commands.
3. **Type consistency:** Types match across tasks - sql_iterator<T>, sql_sentinel, vector versions properly named.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-04-iterator-implementation.md`. Two execution options:

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?