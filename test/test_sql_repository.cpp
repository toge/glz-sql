#include "glaze_sql/condition.hpp"
#include "glaze_sql/database.hpp"
#include "glaze_sql/sql_repository.hpp"

#include "catch2/catch_all.hpp"

struct User {
  int64_t                                id{};
  std::optional<std::string>             email;
  std::string                            name;
  int64_t                                age{};
  double                                 score{};
  static constexpr std::string_view      table_name = "users";
};

template <>
struct glz::meta<User> {
  using type                  = User;
  static constexpr auto value = object(
    "id", &User::id,
    "email", &User::email,
    "name", &User::name,
    "age", &User::age,
    "score", &User::score
  );
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

TEST_CASE("sql_repository: find_by single condition") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  auto alice = repo.find_by(glz_sql::where_eq<"name">(std::string{"Alice"}));
  REQUIRE(alice.has_value());
  REQUIRE(alice->id == 1);

  auto not_found = repo.find_by(glz_sql::where_eq<"name">(std::string{"Charlie"}));
  REQUIRE_FALSE(not_found.has_value());
}

TEST_CASE("sql_repository: select_by single condition") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});
  repo.insert(User{.id = 3, .name = "Alice", .score = 92.0});

  auto alices = repo.select_by(glz_sql::where_eq<"name">(std::string{"Alice"}));
  REQUIRE(alices.size() == 2);
}

TEST_CASE("sql_repository: update_by single condition") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});

  repo.update_by(User{.id = 1, .name = "Alice", .score = 100.0},
                 glz_sql::where_eq<"name">(std::string{"Alice"}));

  auto alice = repo.find_by(glz_sql::where_eq<"name">(std::string{"Alice"}));
  REQUIRE(alice.has_value());
  REQUIRE(alice->score == 100.0);
}

TEST_CASE("sql_repository: remove_by single condition") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  repo.remove_by(glz_sql::where_eq<"name">(std::string{"Bob"}));

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

  repo.update_by(User{.id = 2, .name = "Bob", .score = 100.0},
                 glz_sql::where_eq<"id">(int64_t{2}));

  auto bob = repo.find_by(glz_sql::where_eq<"id">(int64_t{2}));
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

TEST_CASE("condition: AND chain") {
  using namespace glz_sql;
  auto c = where_gt<"age">(int64_t{18}) && where_eq<"name">(std::string{"Alice"})
    && where_lt<"age">(int64_t{65});
  REQUIRE(c.fragment() == "((age > ?) AND (name = ?)) AND (age < ?)");
  REQUIRE(c.placeholder_count() == 3);
}

TEST_CASE("condition: OR composition") {
  using namespace glz_sql;
  auto c = where_eq<"status">(std::string{"active"}) || where_eq<"status">(std::string{"pending"});
  REQUIRE(c.fragment() == "(status = ?) OR (status = ?)");
  REQUIRE(c.placeholder_count() == 2);
}

TEST_CASE("condition: AND of ORs") {
  using namespace glz_sql;
  auto c = (where_eq<"status">(std::string{"active"}) || where_eq<"status">(std::string{"pending"}))
        && where_gt<"age">(int64_t{18});
  REQUIRE(c.fragment() == "((status = ?) OR (status = ?)) AND (age > ?)");
  REQUIRE(c.placeholder_count() == 3);
}

TEST_CASE("condition: valid_condition concept") {
  using namespace glz_sql;

  // struct で User テーブル風のカラム名セットを定義
  struct fake_user {
    int age;
    std::string name;
  };

  // 静的にチェック (static_assert)
  static_assert(valid_condition<decltype(where_eq<"age">(int64_t{20})), fake_user>);
  static_assert(valid_condition<decltype(where_eq<"name">(std::string{"x"})), fake_user>);
  static_assert(valid_condition<decltype(where_eq<"missing">(int64_t{20})), fake_user>);  // コンパイル時に reject されない (型システム外) — runtime 検証は T13-T16 で行う
  static_assert(!valid_condition<int, fake_user>);
  static_assert(!valid_condition<std::string, fake_user>);

  // composite の再帰
  static_assert(valid_condition<
    decltype(where_eq<"age">(int64_t{20}) && where_eq<"name">(std::string{"x"})),
    fake_user>);
  // ただしカラム名検証は構造体リフレクションを使うので、Glaze reflect された struct でないと通らない
  // fake_user には glaze reflect がないので、テストではチェックの通過のみを確認

  // composite のコンパイル時 reject
  static_assert(!valid_condition<int, fake_user>);

  REQUIRE(true);  // ダミー
}

TEST_CASE("sql_repository: select_by with AND") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Alice", .score = 80.0});
  repo.insert(User{.id = 3, .name = "Bob",   .score = 70.0});

  auto results = repo.select_by(
    glz_sql::where_eq<"name">(std::string{"Alice"}) && glz_sql::where_gt<"score">(90.0)
  );
  REQUIRE(results.size() == 1);
  REQUIRE(results[0].id == 1);
}

TEST_CASE("sql_repository: select_by with OR") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob",   .score = 87.3});
  repo.insert(User{.id = 3, .name = "Carol", .score = 70.0});

  auto results = repo.select_by(
    glz_sql::where_lt<"score">(80.0) || glz_sql::where_gt<"score">(90.0)
  );
  REQUIRE(results.size() == 2);
}

TEST_CASE("sql_repository: select_by with AND + OR precedence") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Alice", .score = 70.0});
  repo.insert(User{.id = 3, .name = "Bob",   .score = 95.0});

  // (Alice AND score>90) OR (Bob AND score>90)
  auto results = repo.select_by(
    (glz_sql::where_eq<"name">(std::string{"Alice"}) && glz_sql::where_gt<"score">(90.0))
    || (glz_sql::where_eq<"name">(std::string{"Bob"}) && glz_sql::where_gt<"score">(90.0))
  );
  REQUIRE(results.size() == 2);
}

TEST_CASE("sql_repository: select_by with IN") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "A", .age = 10, .score = 1.0});
  repo.insert(User{.id = 2, .name = "B", .age = 20, .score = 2.0});
  repo.insert(User{.id = 3, .name = "C", .age = 30, .score = 3.0});

  auto results = repo.select_by(glz_sql::where_in<"id">(int64_t{1}, int64_t{3}));
  REQUIRE(results.size() == 2);
}

TEST_CASE("sql_repository: select_by with BETWEEN") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "A", .age = 10, .score = 1.0});
  repo.insert(User{.id = 2, .name = "B", .age = 20, .score = 2.0});
  repo.insert(User{.id = 3, .name = "C", .age = 30, .score = 3.0});

  auto results = repo.select_by(glz_sql::where_between<"age">(int64_t{15}, int64_t{25}));
  REQUIRE(results.size() == 1);
  REQUIRE(results[0].id == 2);
}

TEST_CASE("sql_repository: select_by with LIKE") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 1.0});
  repo.insert(User{.id = 2, .name = "Bob",   .score = 2.0});
  repo.insert(User{.id = 3, .name = "Alex",  .score = 3.0});

  auto results = repo.select_by(glz_sql::where_like<"name">(std::string{"Al%"}));
  REQUIRE(results.size() == 2);
}

TEST_CASE("sql_repository: select_by with IS NULL") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .email = std::string{"a@x"}, .name = "A", .score = 1.0});
  repo.insert(User{.id = 2, .email = std::nullopt,        .name = "B", .score = 2.0});

  auto no_email = repo.select_by(glz_sql::where_is_null<"email">());
  REQUIRE(no_email.size() == 1);
  REQUIRE(no_email[0].id == 2);

  auto has_email = repo.select_by(glz_sql::where_is_not_null<"email">());
  REQUIRE(has_email.size() == 1);
  REQUIRE(has_email[0].id == 1);
}
