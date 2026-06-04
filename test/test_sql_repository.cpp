#include "glaze_sql/condition.hpp"
#include "glaze_sql/database.hpp"
#include "glaze_sql/sql_repository.hpp"

#include "catch2/catch_all.hpp"

struct User {
  int64_t                           id{};
  std::string                       name;
  double                            score{};
  static constexpr std::string_view table_name = "users";
};

template <>
struct glz::meta<User> {
  using type                  = User;
  static constexpr auto value = object("id", &User::id, "name", &User::name, "score", &User::score);
};

struct Product {
  int64_t                           id{};
  std::string                       name;
  double                            price{};
  static constexpr std::string_view table_name = "products";
};

template <>
struct glz::meta<Product> {
  using type                  = Product;
  static constexpr auto value = object("id", &Product::id, "name", &Product::name, "price", &Product::price);
};

TEST_CASE("sql_repository: create_table") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);

  REQUIRE(repo.create_table());
}

TEST_CASE("sql_repository: insert and select_all") {
  glz_sql::sqlite_database      db(":memory:");
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
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  auto alice = repo.find_by<"name">("Alice");
  REQUIRE(alice.has_value());
  REQUIRE(alice->id == 1);
  REQUIRE(alice->name == "Alice");
  REQUIRE(alice->score == 95.5);

  auto not_found = repo.find_by<"name">("Charlie");
  REQUIRE_FALSE(not_found.has_value());
}

TEST_CASE("sql_repository: select_by") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});
  repo.insert(User{.id = 3, .name = "Alice", .score = 92.0});

  auto alices = repo.select_by<"name">("Alice");
  REQUIRE(alices.size() == 2);
}

TEST_CASE("sql_repository: update_by") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});

  repo.update_by<"name">(User{.id = 1, .name = "Alice", .score = 100.0}, "Alice");

  auto alice = repo.find_by<"name">("Alice");
  REQUIRE(alice.has_value());
  REQUIRE(alice->score == 100.0);
}

TEST_CASE("sql_repository: remove_by") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  repo.remove_by<"name">("Bob");

  auto all = repo.select_all();
  REQUIRE(all.size() == 1);
  REQUIRE(all[0].name == "Alice");
}

TEST_CASE("sql_repository: multiple tables") {
  glz_sql::sqlite_database         db(":memory:");
  glz_sql::sql_repository<User>    user_repo(db);
  glz_sql::sql_repository<Product> product_repo(db);

  user_repo.create_table();
  product_repo.create_table();

  user_repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  product_repo.insert(Product{.id = 1, .name = "Widget", .price = 9.99});

  auto users    = user_repo.select_all();
  auto products = product_repo.select_all();

  REQUIRE(users.size() == 1);
  REQUIRE(products.size() == 1);
}

TEST_CASE("sql_repository: update_by by id column") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  repo.update_by<"id">(User{.id = 2, .name = "Bob", .score = 100.0}, int64_t{2});

  auto bob = repo.find_by<"id">(int64_t{2});
  REQUIRE(bob.has_value());
  REQUIRE(bob->score == 100.0);
}

TEST_CASE("sql_repository: compile-time column validation") {
  // コンパイル時に有効/無効なカラム名を検証する
  static_assert(glz_sql::valid_column<"name", User>);
  static_assert(glz_sql::valid_column<"id", User>);
  static_assert(glz_sql::valid_column<"score", User>);
  static_assert(!glz_sql::valid_column<"invalid_column", User>);
  static_assert(!glz_sql::valid_column<"", User>);

  SUCCEED("valid_column concept correctly validates column names at compile time");
}

TEST_CASE("condition: where_eq") {
  auto c    = glz_sql::where_eq<"name">(std::string{"Alice"});
  auto frag = c.fragment();
  REQUIRE(frag == "name = ?");
  REQUIRE(c.placeholder_count() == 1);
}

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

TEST_CASE("condition: where_like") {
  using namespace glz_sql;
  REQUIRE((where_like<"name">(std::string{"Al%"}).fragment()) == "name LIKE ?");
  REQUIRE(where_like<"name">(std::string{"Al%"}).placeholder_count() == 1);
}

TEST_CASE("condition: where_in") {
  using namespace glz_sql;
  auto c = where_in<"id">(int64_t{1}, int64_t{3}, int64_t{5});
  REQUIRE(c.fragment() == "id IN (?, ?, ?)");
  REQUIRE(c.placeholder_count() == 3);
}

TEST_CASE("condition: where_between") {
  using namespace glz_sql;
  auto c = where_between<"age">(int64_t{20}, int64_t{30});
  REQUIRE(c.fragment() == "age BETWEEN ? AND ?");
  REQUIRE(c.placeholder_count() == 2);
}

TEST_CASE("condition: is_null / is_not_null") {
  using namespace glz_sql;
  REQUIRE((where_is_null<"email">().fragment()) == "email IS NULL");
  REQUIRE(where_is_null<"email">().placeholder_count() == 0);
  REQUIRE((where_is_not_null<"email">().fragment()) == "email IS NOT NULL");
  REQUIRE(where_is_not_null<"email">().placeholder_count() == 0);
}

TEST_CASE("condition: AND composition") {
  using namespace glz_sql;
  auto c = where_gt<"age">(int64_t{18}) && where_eq<"name">(std::string{"Alice"});
  REQUIRE(c.fragment() == "(age > ?) AND (name = ?)");
  REQUIRE(c.placeholder_count() == 2);
}
