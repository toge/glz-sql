#include "catch2/catch_all.hpp"
#include "glaze_sql/sqlite_bind.hpp"

TEST_CASE("sqlite_bind header compiles") {
  // Type traits compile-time checks
  STATIC_REQUIRE(std::is_same_v<decltype(glz_sql::sqlite_type_traits<int64_t>::sql_type), const char* const>);
  STATIC_REQUIRE(std::is_same_v<decltype(glz_sql::sqlite_type_traits<double>::sql_type), const char* const>);
  STATIC_REQUIRE(std::is_same_v<decltype(glz_sql::sqlite_type_traits<std::string>::sql_type), const char* const>);

  REQUIRE(std::string(glz_sql::sqlite_type_traits<int64_t>::sql_type) == "INTEGER");
  REQUIRE(std::string(glz_sql::sqlite_type_traits<double>::sql_type) == "REAL");
  REQUIRE(std::string(glz_sql::sqlite_type_traits<std::string>::sql_type) == "TEXT");
}
