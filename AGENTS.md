# AGENTS.md — glaze-sql

## 概要

Glaze リフレクションを活用した C++ ヘッダオンリーの SQLite ORM ライブラリ。
任意の構造体を `sql_table` コンセプトで定義すると、型安全な CRUD 操作とコンパイル時 WHERE 条件構築が可能になる。

## ビルドとテスト

```bash
# ビルド（vcpkg 使用、Linux x64）
./build.sh

# テスト実行
./test.sh          # build/ で ctest -V を実行
```

- パッケージマネージャ: vcpkg（`~/vm/vcpkg`）がデフォルト。`conanfile.py` が存在すれば Conan に切替
- vcpkg 依存: `glaze`, `sqlite3`, `catch2`
- C++ 標準: C++23
- `VCPKG_ROOT` は `build.sh` 内で `~/vm/vcpkg` に固定。環境変数で上書き可能

## ディレクトリ構成

```
include/glz-sql/    # ライブラリ本体（ヘッダオンリー）
  sql_repository.hpp  # CRUD リポジトリ（メイン）
  condition.hpp       # WHERE 条件（NTTP + 演算子オーバーロード）
  database.hpp        # SQLite 接続インターフェース
  sqlite_bind.hpp     # C++型 ↔ SQLite 型マッピング
  fixed_string.hpp    # コンパイル時文字列（NTTP 用）
test/                 # Catch2 テスト
  test_sql_repository.cpp  # リポジトリ + 条件のテスト
  test_sqlite_bind.cpp     # 型バインドのテスト
  test_trim.cpp            # 未使用テスト（空）
```

## アーキテクチャの要点

- **`sql_repository<T>`**: `T::table_name`（`std::string_view`）と `glz::meta<T>` が必要。`sql_table` コンセプトで制約
- **条件構築**: `where_eq<"column">(value)` のように NTTP でカラム名を指定。`&&` / `||` で合成可能。`valid_column<"col", T>` でコンパイル時にカラム存在を検証
- **対応条件**: `eq`, `ne`, `lt`, `le`, `gt`, `ge`, `like`, `in`, `between`, `is_null`, `is_not_null`
- **対応型**: `int64_t`, `double`, `std::string`, `std::string_view`, `const char*`, `char[N]`, `std::optional<T>`（NULL 許容）
- **`database_interface`**: 抽象クラス。`sqlite_database` が具象実装。`:memory:` でインメモリ DB
- **名前空間**: 全て `glz_sql` 内

## コーディング規約

- **フォーマッタ**: `.clang-format`（LLVM ベース、インデント 2 スペース、列制限 200）
- **リンター**: `.clang-tidy`（bugprone, cppcoreguidelines, modernize, performance 系有効）
- **命名規則**（`.clang-tidy` で強制）:
  - クラス/列挙型: `lower_case`
  - 関数: `camelBack`
  - 変数/メンバ/パラメータ: `lower_case`
  - 定数/constexpr: `UPPER_CASE`
- **エディタ設定**: `.editorconfig`（UTF-8, LF, トリム有効）
- コメントは日本語で記述（既存コードに準拠）

## テスト作法

- Catch2 を使用。テストファイルは `test/test_*.cpp`
- 全テストを `add_executable(all_test ...)` で一括ビルド
- テスト内で `glz_sql::sqlite_database db(":memory:")` を生成し、毎テスト独立したインメモリ DB を使用
- テスト構造体はテストファイル内で定義（例: `User`, `Product`）。`glz::meta<T>` の特化もテスト内に記述

## 注意点

- `build.sh`, `test.sh` は `.gitignore` に含まれる（リポジトリ管理外）
- ルートの `test01.cpp` ～ `test100.cpp` は CMakeLists.txt の `foreach` で個別ビルドされるが、現在は存在しない
- `fixed_string` は NTTP 専用。`consteval` コンストラクタを持つため、実行時構築は不可
- `sqlite_type_traits` に未対応型を渡すと `static_assert` でコンパイルエラー
- `std::optional<std::optional<T>>` のネストは禁止（`static_assert` で検出）
