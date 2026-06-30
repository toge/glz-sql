#include "catch2/catch_all.hpp"
#include "glz-sql/database.hpp"
#include "glz-sql/sqlite_bind.hpp"

#include <chrono>
#include <filesystem>

TEST_CASE("sqlite_bind header compiles") {
  // Type traits compile-time checks
  STATIC_REQUIRE(std::is_same_v<decltype(glz_sql::sqlite_type_traits<int64_t>::sql_type), const char* const>);
  STATIC_REQUIRE(std::is_same_v<decltype(glz_sql::sqlite_type_traits<double>::sql_type), const char* const>);
  STATIC_REQUIRE(std::is_same_v<decltype(glz_sql::sqlite_type_traits<std::string>::sql_type), const char* const>);

  REQUIRE(std::string(glz_sql::sqlite_type_traits<int64_t>::sql_type) == "INTEGER");
  REQUIRE(std::string(glz_sql::sqlite_type_traits<double>::sql_type) == "REAL");
  REQUIRE(std::string(glz_sql::sqlite_type_traits<std::string>::sql_type) == "TEXT");
}

TEST_CASE("sqlite_database: execute accepts non-null-terminated string_view") {
  glz_sql::sqlite_database db(":memory:");

  auto const prefix   = std::string{"CREATE TABLE users(id INTEGER);"};
  auto const sql      = prefix + " INVALID";
  auto const sql_view = std::string_view(sql.data(), prefix.size());

  REQUIRE(db.execute(sql_view));
  REQUIRE(db.execute("INSERT INTO users(id) VALUES (1);"));
}

TEST_CASE("sqlite_database: constructor accepts non-null-terminated path view") {
  auto const unique_id   = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  auto const db_path     = std::filesystem::temp_directory_path() / std::filesystem::path("glz-sql-" + unique_id + ".db");
  auto const wrapped     = db_path.string() + "-trailing";
  auto const path_view   = std::string_view(wrapped.data(), db_path.string().size());
  auto const wrapped_db  = std::filesystem::path(wrapped);
  auto const cleanup_one = [](std::filesystem::path const& path) {
    auto ec = std::error_code{};
    std::filesystem::remove(path, ec);
  };

  cleanup_one(db_path);
  cleanup_one(wrapped_db);

  {
    glz_sql::sqlite_database db(path_view);
    REQUIRE(db.execute("CREATE TABLE users(id INTEGER);"));
  }

  REQUIRE(std::filesystem::exists(db_path));
  REQUIRE_FALSE(std::filesystem::exists(wrapped_db));

  cleanup_one(db_path);
  cleanup_one(wrapped_db);
}
