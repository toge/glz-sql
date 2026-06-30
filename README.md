# glaze-sql

[Glaze](https://github.com/stephenberry/glaze) リフレクションを活用した C++ ヘッダオンリーの SQLite ORM ライブラリ。

## 特徴

- **型安全な CRUD**: 構造体を定義するだけで INSERT / SELECT / UPDATE / DELETE が使用可能
- **コンパイル時 WHERE 条件**: NTTP（Non-Type Template Parameter）によるカラム名指定で、コンパイル時にカラム存在を検証
- **条件の合成**: `&&` / `||` 演算子で条件を自由に組み合わせ可能
- **NULL 許容**: `std::optional<T>` で NULL 値を型安全に扱える
- **ヘッダオンリー**: ライブラリ本体はヘッダオンリー。利用時は SQLite3 へのリンクが必要

## 対応条件

| 関数 | SQL |
|------|-----|
| `where_eq<"col">(v)` | `col = ?` |
| `where_ne<"col">(v)` | `col != ?` |
| `where_lt<"col">(v)` | `col < ?` |
| `where_le<"col">(v)` | `col <= ?` |
| `where_gt<"col">(v)` | `col > ?` |
| `where_ge<"col">(v)` | `col >= ?` |
| `where_like<"col">(v)` | `col LIKE ?` |
| `where_in<"col">(v1, v2, ...)` | `col IN (?, ?, ...)` |
| `where_between<"col">(lo, hi)` | `col BETWEEN ? AND ?` |
| `where_is_null<"col">()` | `col IS NULL` |
| `where_is_not_null<"col">()` | `col IS NOT NULL` |

## 必要環境

- C++23
- CMake 3.25+
- vcpkg

### 依存ライブラリ

- [glaze](https://github.com/stephenberry/glaze) — JSON / リフレクション
- [SQLite3](https://sqlite.org/) — データベースエンジン
- [Catch2](https://github.com/catchorg/Catch2) — テストフレームワーク（テスト時のみ）

## 使い方

### 構造体の定義

`sql_repository<T>` / `sql_iterator<T>` の行型は、デフォルト構築可能で、各フィールドへ読み出し結果を書き戻せる必要があります。`const` フィールドやネストした `std::optional` は非対応です。

TEXT 列は、読み出し後も所有権を保てる `std::string` または `std::optional<std::string>` を使用してください。`std::string_view` / `const char*` は条件値や一時的なバインド値には使えますが、リポジトリの行型メンバとしては非対応です。

```cpp
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>

struct User {
  int64_t     id{};
  std::string name;
  int64_t     age{};
  double      score{};

  // テーブル名を static constexpr で定義
  static constexpr std::string_view table_name = "users";
};

// Glaze リフレクション定義
template <>
struct glz::meta<User> {
  using type = User;
  static constexpr auto value = glz::object(
    "id",    &User::id,
    "name",  &User::name,
    "age",   &User::age,
    "score", &User::score
  );
};
```

### CRUD 操作

```cpp
#include "glz-sql/sql_repository.hpp"
#include "glz-sql/database.hpp"

// データベース接続（インメモリ）
glz_sql::sqlite_database      db(":memory:");
glz_sql::sql_repository<User> repo(db);

// CMake では SQLite3 も含めて公開ターゲットをリンク
// target_link_libraries(your_target PRIVATE glz-sql::glz-sql)

// テーブル作成
repo.create_table();

// INSERT
repo.insert(User{.id = 1, .name = "Alice", .age = 30, .score = 95.5});

// SELECT（全件）— range-for で直接走査可能
for (auto const& row : repo.select_all()) {
  std::cout << row.name << "\n";
}

// SELECT（条件付き）
for (auto const& row : repo.select_by(
  glz_sql::where_gt<"age">(int64_t{20}) && glz_sql::where_like<"name">(std::string{"Al%"})
)) {
  std::cout << row.name << "\n";
}

// vector に変換したい場合
auto results = std::ranges::to<std::vector>(repo.select_by(
  glz_sql::where_eq<"age">(int64_t{25})
));

// 1件検索
auto user = repo.find_by(glz_sql::where_eq<"id">(int64_t{1}));

// UPDATE
repo.update_by(
  User{.id = 1, .name = "Alice", .age = 31, .score = 100.0},
  glz_sql::where_eq<"id">(int64_t{1})
);

// DELETE
repo.remove_by(glz_sql::where_eq<"id">(int64_t{1}));
```

## ライセンス

[MIT License](LICENSE)
