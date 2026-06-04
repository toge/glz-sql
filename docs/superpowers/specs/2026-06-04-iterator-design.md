# Iterator Design for glaze-sql

## 概要

`select_all` と `select_by` が返す型を `std::vector<T>` からイテレータペアに変更し、`std::ranges` で処理できるようにする。

## 要件

1. `select_all()` と `select_by(cond)` がiteratorとsentinelのペアを返す
2. iteratorは`std::input_iterator_tag`を満たし、`std::ranges::input_range`として使用可能
3. iteratorは`sqlite3_stmt`の生命周期を`unique_ptr`で管理
4. 現在の行をキャッシュし、`operator*`で返す
5. `operator++`で`sqlite3_step`を呼び、次の行をキャッシュ
6. sentinelは「行が存在しない」状態を表す
7. 既存のvector版は維持（メソッド名を変更）

## 設計

### sql_iterator<T>クラス

```cpp
template <typename T>
class sql_iterator {
public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;
    using reference = const T&;
    using pointer = const T*;

    // コンストラクタ
    sql_iterator(database_interface& db, std::string_view sql);
    
    // 条件付きコンストラクタ
    template <typename Cond>
    sql_iterator(database_interface& db, std::string_view sql, const Cond& cond);
    
    // デストラクタ
    ~sql_iterator() = default;
    
    // ムーブのみ許可
    sql_iterator(sql_iterator&&) = default;
    sql_iterator& operator=(sql_iterator&&) = default;
    
    // コピー禁止
    sql_iterator(const sql_iterator&) = delete;
    sql_iterator& operator=(const sql_iterator&) = delete;
    
    // 演算子
    const T& operator*() const;
    sql_iterator& operator++();
    bool operator==(const sql_sentinel&) const;
    
private:
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt_;
    std::optional<T> current_;
    
    void advance();
};
```

### sql_sentinelクラス

```cpp
class sql_sentinel {
public:
    sql_sentinel() = default;
};
```

### select_all/select_byの変更

```cpp
// iterator版
auto select_all() const -> std::pair<sql_iterator<T>, sql_sentinel>;
template <typename Cond>
auto select_by(const Cond& cond) const -> std::pair<sql_iterator<T>, sql_sentinel>;

// vector版（既存）
auto select_all_vec() const -> std::vector<T>;
template <typename Cond>
auto select_by_vec(const Cond& cond) const -> std::vector<T>;
```

## 使用例

```cpp
glz_sql::sqlite_database db(":memory:");
glz_sql::sql_repository<User> repo(db);

// iterator版
auto [it, end] = repo.select_all();
for (auto&& record : std::ranges::subrange(it, end)) {
    std::println("User: {}", record.name);
}

// std::ranges::filter_view
auto [it2, end2] = repo.select_by(where_eq<"age">(20));
auto filtered = std::ranges::filter_view(
    std::ranges::subrange(it2, end2),
    [](const User& u) { return u.name.starts_with("A"); });
for (auto&& user : filtered) {
    std::println("User: {}", user.name);
}

// vector版（既存）
auto users = repo.select_all_vec();
```

## 既存コードへの影響

1. `select_all()` と `select_by()` の戻り値が変更される
2. 既存の使用箇所は `select_all_vec()` または `select_by_vec()` に変更が必要
3. テストコードの更新が必要

## テスト計画

1. iterator基本機能テスト
2. sentinelとの比較テスト
3. std::ranges::for_eachテスト
4. std::ranges::filter_viewテスト
5. エラーハンドリングテスト（空の結果、DBエラー）
6. 既存テストの動作確認（vector版）