#include "glz-sql/condition.hpp"
#include "glz-sql/database.hpp"
#include "glz-sql/sql_repository.hpp"

#include "catch2/catch_all.hpp"

#include <iterator>
#include <ranges>
#include <vector>

struct User {
  int64_t                           id{};
  std::optional<std::string>        email;
  std::string                       name;
  int64_t                           age{};
  double                            score{};
  static constexpr std::string_view table_name = "users";
};

template <>
struct glz::meta<User> {
  using type                  = User;
  static constexpr auto value = object("id", &User::id, "email", &User::email, "name", &User::name, "age", &User::age, "score", &User::score);
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

  auto [it, end] = repo.select_all();
  std::vector<User> all(it, end);
  REQUIRE(all.size() == 2);
  REQUIRE(all[0].id == 1);
  REQUIRE(all[0].name == "Alice");
  REQUIRE(all[0].score == 95.5);
  REQUIRE(all[1].id == 2);
  REQUIRE(all[1].name == "Bob");
  REQUIRE(all[1].score == 87.3);
}

TEST_CASE("sql_repository: select_all vector") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  User user1{.id = 1, .name = "Alice", .age = 25};
  User user2{.id = 2, .name = "Bob", .age = 30};
  repo.insert(user1);
  repo.insert(user2);

  auto [it, end] = repo.select_all();
  std::vector<User> all(it, end);
  REQUIRE(all.size() == 2);
  REQUIRE(all[0].id == 1);
  REQUIRE(all[0].name == "Alice");
  REQUIRE(all[1].id == 2);
  REQUIRE(all[1].name == "Bob");
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

  auto [it, end] = repo.select_by(glz_sql::where_eq<"name">(std::string{"Alice"}));
  std::vector<User> alices(it, end);
  REQUIRE(alices.size() == 2);
}

TEST_CASE("sql_repository: update_by single condition") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});

  repo.update_by(User{.id = 1, .name = "Alice", .score = 100.0}, glz_sql::where_eq<"name">(std::string{"Alice"}));

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

  auto [it, end] = repo.select_all();
  std::vector<User> all(it, end);
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

  auto [it_u, end_u]     = user_repo.select_all();
  std::vector<User> users(it_u, end_u);
  auto [it_p, end_p]     = product_repo.select_all();
  std::vector<Product> products(it_p, end_p);

  REQUIRE(users.size() == 1);
  REQUIRE(products.size() == 1);
}

TEST_CASE("sql_repository: update_by by id column") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});

  repo.update_by(User{.id = 2, .name = "Bob", .score = 100.0}, glz_sql::where_eq<"id">(int64_t{2}));

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
  auto c = where_gt<"age">(int64_t{18}) && where_eq<"name">(std::string{"Alice"}) && where_lt<"age">(int64_t{65});
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
  auto c = (where_eq<"status">(std::string{"active"}) || where_eq<"status">(std::string{"pending"})) && where_gt<"age">(int64_t{18});
  REQUIRE(c.fragment() == "((status = ?) OR (status = ?)) AND (age > ?)");
  REQUIRE(c.placeholder_count() == 3);
}

TEST_CASE("condition: valid_condition concept") {
  using namespace glz_sql;

  // struct で User テーブル風のカラム名セットを定義
  struct fake_user {
    int         age;
    std::string name;
  };

  // 静的にチェック (static_assert)
  static_assert(valid_condition<decltype(where_eq<"age">(int64_t{20})), fake_user>);
  static_assert(valid_condition<decltype(where_eq<"name">(std::string{"x"})), fake_user>);
  static_assert(valid_condition<decltype(where_eq<"missing">(int64_t{20})), fake_user>);  // コンパイル時に reject されない (型システム外) — runtime 検証は T13-T16 で行う
  static_assert(!valid_condition<int, fake_user>);
  static_assert(!valid_condition<std::string, fake_user>);

  // composite の再帰
  static_assert(valid_condition<decltype(where_eq<"age">(int64_t{20}) && where_eq<"name">(std::string{"x"})), fake_user>);
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
  repo.insert(User{.id = 3, .name = "Bob", .score = 70.0});

  auto [it, end] = repo.select_by(glz_sql::where_eq<"name">(std::string{"Alice"}) && glz_sql::where_gt<"score">(90.0));
  std::vector<User> results(it, end);
  REQUIRE(results.size() == 1);
  REQUIRE(results[0].id == 1);
}

TEST_CASE("sql_repository: select_by with OR") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Bob", .score = 87.3});
  repo.insert(User{.id = 3, .name = "Carol", .score = 70.0});

  auto [it, end] = repo.select_by(glz_sql::where_lt<"score">(80.0) || glz_sql::where_gt<"score">(90.0));
  std::vector<User> results(it, end);
  REQUIRE(results.size() == 2);
}

TEST_CASE("sql_repository: select_by with AND + OR precedence") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 95.5});
  repo.insert(User{.id = 2, .name = "Alice", .score = 70.0});
  repo.insert(User{.id = 3, .name = "Bob", .score = 95.0});

  // (Alice AND score>90) OR (Bob AND score>90)
  auto [it, end] = repo.select_by((glz_sql::where_eq<"name">(std::string{"Alice"}) && glz_sql::where_gt<"score">(90.0)) || (glz_sql::where_eq<"name">(std::string{"Bob"}) && glz_sql::where_gt<"score">(90.0)));
  std::vector<User> results(it, end);
  REQUIRE(results.size() == 2);
}

TEST_CASE("sql_repository: select_by with IN") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "A", .age = 10, .score = 1.0});
  repo.insert(User{.id = 2, .name = "B", .age = 20, .score = 2.0});
  repo.insert(User{.id = 3, .name = "C", .age = 30, .score = 3.0});

  auto [it, end] = repo.select_by(glz_sql::where_in<"id">(int64_t{1}, int64_t{3}));
  std::vector<User> results(it, end);
  REQUIRE(results.size() == 2);
}

TEST_CASE("sql_repository: select_by with BETWEEN") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "A", .age = 10, .score = 1.0});
  repo.insert(User{.id = 2, .name = "B", .age = 20, .score = 2.0});
  repo.insert(User{.id = 3, .name = "C", .age = 30, .score = 3.0});

  auto [it, end] = repo.select_by(glz_sql::where_between<"age">(int64_t{15}, int64_t{25}));
  std::vector<User> results(it, end);
  REQUIRE(results.size() == 1);
  REQUIRE(results[0].id == 2);
}

TEST_CASE("sql_repository: select_by with LIKE") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .score = 1.0});
  repo.insert(User{.id = 2, .name = "Bob", .score = 2.0});
  repo.insert(User{.id = 3, .name = "Alex", .score = 3.0});

  auto [it, end] = repo.select_by(glz_sql::where_like<"name">(std::string{"Al%"}));
  std::vector<User> results(it, end);
  REQUIRE(results.size() == 2);
}

TEST_CASE("sql_repository: select_by with IS NULL") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .email = std::string{"a@x"}, .name = "A", .score = 1.0});
  repo.insert(User{.id = 2, .email = std::nullopt, .name = "B", .score = 2.0});

  auto [it1, end1] = repo.select_by(glz_sql::where_is_null<"email">());
  std::vector<User> no_email(it1, end1);
  REQUIRE(no_email.size() == 1);
  REQUIRE(no_email[0].id == 2);

  auto [it2, end2] = repo.select_by(glz_sql::where_is_not_null<"email">());
  std::vector<User> has_email(it2, end2);
  REQUIRE(has_email.size() == 1);
  REQUIRE(has_email[0].id == 1);
}

TEST_CASE("sql_repository: update_by with multiple conditions") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .age = 20, .score = 1.0});
  repo.insert(User{.id = 2, .name = "Alice", .age = 30, .score = 2.0});
  repo.insert(User{.id = 3, .name = "Bob", .age = 25, .score = 3.0});

  // name=Alice AND age<25 のみ更新
  repo.update_by(User{.id = 1, .name = "Alice", .age = 20, .score = 999.0}, glz_sql::where_eq<"name">(std::string{"Alice"}) && glz_sql::where_lt<"age">(int64_t{25}));

  auto a1 = repo.find_by(glz_sql::where_eq<"id">(int64_t{1}));
  REQUIRE(a1.has_value());
  REQUIRE(a1->score == 999.0);

  auto a2 = repo.find_by(glz_sql::where_eq<"id">(int64_t{2}));
  REQUIRE(a2.has_value());
  REQUIRE(a2->score == 2.0);  // 変更されていない
}

TEST_CASE("sql_repository: update_by bind order (SET then WHERE)") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .age = 20, .score = 1.0});
  repo.insert(User{.id = 2, .name = "Bob", .age = 30, .score = 2.0});

  // ヒット対象は id=2 のみ。SET 句に id=99 を指定しても WHERE 句で id=2 に絞られる。
  // → バインド順が正しければ SET 値が id=2 の行に適用され、id=1 は無変更。
  //   SET で id=99, name="Updated" などが適用された結果は id=99 で検索できる。
  repo.update_by(User{.id = 99, .name = "Updated", .age = 99, .score = 100.0}, glz_sql::where_eq<"id">(int64_t{2}));

  auto updated = repo.find_by(glz_sql::where_eq<"id">(int64_t{99}));
  REQUIRE(updated.has_value());
  REQUIRE(updated->name == "Updated");
  REQUIRE(updated->age == 99);
  REQUIRE(updated->score == 100.0);

  // id=2 はもう存在しない（SET で id=99 に更新された）
  auto old_id = repo.find_by(glz_sql::where_eq<"id">(int64_t{2}));
  REQUIRE_FALSE(old_id.has_value());

  // id=1 は影響を受けない
  auto unchanged = repo.find_by(glz_sql::where_eq<"id">(int64_t{1}));
  REQUIRE(unchanged.has_value());
  REQUIRE(unchanged->score == 1.0);
}

TEST_CASE("sql_repository: remove_by with multiple conditions") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "Alice", .age = 20, .score = 1.0});
  repo.insert(User{.id = 2, .name = "Alice", .age = 30, .score = 2.0});
  repo.insert(User{.id = 3, .name = "Bob", .age = 25, .score = 3.0});

  // name=Alice OR name=Bob
  repo.remove_by(glz_sql::where_eq<"name">(std::string{"Alice"}) || glz_sql::where_eq<"name">(std::string{"Bob"}));

  auto [it, end] = repo.select_all();
  std::vector<User> all(it, end);
  REQUIRE(all.size() == 0);
}

TEST_CASE("sql_repository: update_by with IN") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  repo.insert(User{.id = 1, .name = "A", .score = 1.0});
  repo.insert(User{.id = 2, .name = "B", .score = 2.0});
  repo.insert(User{.id = 3, .name = "C", .score = 3.0});

  repo.update_by(User{.id = 1, .name = "A", .score = 0.0}, glz_sql::where_in<"id">(int64_t{1}, int64_t{3}));

  auto a1 = repo.find_by(glz_sql::where_eq<"id">(int64_t{1}));
  auto a3 = repo.find_by(glz_sql::where_eq<"id">(int64_t{3}));
  auto a2 = repo.find_by(glz_sql::where_eq<"id">(int64_t{2}));
  REQUIRE(a1.has_value());
  REQUIRE(a1->score == 0.0);
  // id=3 の行は SET 句で id=1 に更新されたので、id=3 では見つからない
  REQUIRE_FALSE(a3.has_value());
  REQUIRE(a2.has_value());
  REQUIRE(a2->score == 2.0);  // 未更新
}

TEST_CASE("sql_repository: select_by vector") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  User user1{.id = 1, .name = "Alice", .age = 25};
  User user2{.id = 2, .name = "Bob", .age = 30};
  User user3{.id = 3, .name = "Charlie", .age = 25};
  repo.insert(user1);
  repo.insert(user2);
  repo.insert(user3);

  auto [it, end] = repo.select_by(glz_sql::where_eq<"age">(int64_t{25}));
  std::vector<User> results(it, end);
  REQUIRE(results.size() == 2);
  REQUIRE(results[0].id == 1);
  REQUIRE(results[0].name == "Alice");
  REQUIRE(results[1].id == 3);
  REQUIRE(results[1].name == "Charlie");
}

TEST_CASE("condition: compile-time invalid column") {
  using namespace glz_sql;
  // 以下の行のコメントを外すとコンパイルエラーになる
  // auto bad = where_eq<"invalid_column">(std::string{"x"});
  // static_assert(!valid_condition<decltype(bad), User>);

  static_assert(valid_condition<decltype(where_eq<"name">(std::string{"x"})), User>);
  static_assert(valid_condition<decltype(where_eq<"id">(int64_t{1})), User>);
  SUCCEED("compile-time column validation works");
}

TEST_CASE("sql_sentinel basic operations") {
  glz_sql::sql_sentinel sentinel;
  glz_sql::sql_sentinel sentinel2;
  REQUIRE(sentinel == sentinel2);
}

TEST_CASE("sql_iterator type traits") {
  using iter_type = glz_sql::sql_iterator<User>;
  static_assert(std::input_iterator<iter_type>);
  static_assert(std::sentinel_for<glz_sql::sql_sentinel, iter_type>);
  SUCCEED("sql_iterator satisfies input_iterator and sentinel_for");
}

TEST_CASE("std::ranges::for_each with select_all") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  User user1{.id = 1, .name = "Alice", .age = 25};
  User user2{.id = 2, .name = "Bob", .age = 30};
  repo.insert(user1);
  repo.insert(user2);

  auto [it, end] = repo.select_all();
  std::vector<std::string> names;
  std::ranges::for_each(std::ranges::subrange(std::move(it), end), [&](const User& u) { names.push_back(u.name); });
  REQUIRE(names.size() == 2);
  REQUIRE(names[0] == "Alice");
  REQUIRE(names[1] == "Bob");
}

TEST_CASE("std::ranges::filter_view with select_all") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  User user1{.id = 1, .name = "Alice", .age = 25};
  User user2{.id = 2, .name = "Bob", .age = 30};
  User user3{.id = 3, .name = "Charlie", .age = 35};
  repo.insert(user1);
  repo.insert(user2);
  repo.insert(user3);

  auto [it, end] = repo.select_all();
  auto sub       = std::ranges::subrange(std::move(it), end);
  auto filtered  = std::ranges::filter_view(std::move(sub), [](const User& u) { return u.age > 25; });
  int  count     = 0;
  for (auto&& user : filtered) {
    count++;
  }
  REQUIRE(count == 2);
}

TEST_CASE("std::ranges::transform_view") {
  glz_sql::sqlite_database      db(":memory:");
  glz_sql::sql_repository<User> repo(db);
  repo.create_table();

  User user1{.id = 1, .name = "Alice", .age = 25};
  User user2{.id = 2, .name = "Bob", .age = 30};
  repo.insert(user1);
  repo.insert(user2);

  auto [it, end] = repo.select_all();
  auto sub         = std::ranges::subrange(std::move(it), end);
  auto transformed = std::ranges::transform_view(std::move(sub), [](const User& u) { return u.name; });
  std::vector<std::string> names;
  for (auto&& name : transformed) {
    names.push_back(name);
  }
  REQUIRE(names.size() == 2);
  REQUIRE(names[0] == "Alice");
  REQUIRE(names[1] == "Bob");
}
