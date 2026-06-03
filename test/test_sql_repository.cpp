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

  auto alices = repo.select_by("name", "Alice");
  REQUIRE(alices.size() == 2);
}

TEST_CASE("sql_repository: update_by") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});

  repo.update_by(User{.id = 1, .name = "Alice", .score = 100.0}, "name", "Alice");

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

  repo.remove_by("name", "Bob");

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
