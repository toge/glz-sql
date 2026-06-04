#pragma once

#include "database.hpp"
#include "sqlite_bind.hpp"
#include "sql_repository.hpp"  // fixed_string を使う

#include <concepts>
#include <format>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace glz_sql {

/**
 * @brief サポートされる比較演算子
 */
enum class compare_op {
  eq, ne, lt, le, gt, ge,    // 基本 6 種
  like,                       // 部分一致
  in_op, between,             // 複数値
  is_null, is_not_null,       // NULL 判定
  and_op, or_op               // 論理合成
};

/**
 * @brief 比較演算子が葉 (リーフ条件) かどうか
 */
constexpr auto is_leaf_op(compare_op op) -> bool {
  return op == compare_op::eq || op == compare_op::ne
      || op == compare_op::lt || op == compare_op::le
      || op == compare_op::gt || op == compare_op::ge
      || op == compare_op::like
      || op == compare_op::in_op || op == compare_op::between
      || op == compare_op::is_null || op == compare_op::is_not_null;
}

/**
 * @brief 葉条件の前方宣言
 */
template <compare_op Op, fixed_string C, typename V = void, typename V2 = void>
class leaf_condition;

/**
 * @brief 合成条件の前方宣言
 */
template <compare_op Op, typename L, typename R>
  requires (Op == compare_op::and_op || Op == compare_op::or_op)
class composite_condition;

/**
 * @brief 合成条件 (AND / OR)
 */
template <compare_op Op, typename L, typename R>
  requires (Op == compare_op::and_op || Op == compare_op::or_op)
class composite_condition {
  L left_;
  R right_;

public:
  using is_composite_condition_tag = void;
  using left_type                  = L;
  using right_type                 = R;

  constexpr composite_condition(L l, R r) : left_(std::move(l)), right_(std::move(r)) {}

  /**
   * @brief プレースホルダ数 (左右の葉の合計)
   */
  static constexpr auto placeholder_count() -> size_t {
    return L::placeholder_count() + R::placeholder_count();
  }

  /**
   * @brief SQL フラグメント
   */
  auto fragment() const -> std::string {
    if constexpr (Op == compare_op::and_op) {
      return std::format("({}) AND ({})", left_.fragment(), right_.fragment());
    } else {
      return std::format("({}) OR ({})", left_.fragment(), right_.fragment());
    }
  }

  /**
   * @brief バインド
   */
  auto bind(sqlite3_stmt* stmt, int start_index) const -> int {
    int idx = left_.bind(stmt, start_index);
    return right_.bind(stmt, idx);
  }
};

/**
 * @brief AND 演算子 (leaf + leaf)
 */
template <compare_op LOp, fixed_string LC, typename LV, typename LV2,
          compare_op ROp, fixed_string RC, typename RV, typename RV2>
auto operator&&(const leaf_condition<LOp, LC, LV, LV2>& l,
                const leaf_condition<ROp, RC, RV, RV2>& r)
    -> composite_condition<compare_op::and_op,
                           leaf_condition<LOp, LC, LV, LV2>,
                           leaf_condition<ROp, RC, RV, RV2>> {
  return composite_condition<compare_op::and_op,
                             leaf_condition<LOp, LC, LV, LV2>,
                             leaf_condition<ROp, RC, RV, RV2>>(l, r);
}

/**
 * @brief AND 演算子 (leaf + composite)
 */
template <compare_op LOp, fixed_string LC, typename LV, typename LV2,
          compare_op Op, typename L, typename R>
  requires (Op == compare_op::and_op || Op == compare_op::or_op)
auto operator&&(const leaf_condition<LOp, LC, LV, LV2>& l,
                const composite_condition<Op, L, R>& r)
    -> composite_condition<compare_op::and_op,
                           leaf_condition<LOp, LC, LV, LV2>,
                           composite_condition<Op, L, R>> {
  return composite_condition<compare_op::and_op,
                             leaf_condition<LOp, LC, LV, LV2>,
                             composite_condition<Op, L, R>>(l, r);
}

/**
 * @brief AND 演算子 (composite + leaf)
 */
template <compare_op Op, typename L, typename R,
          compare_op ROp, fixed_string RC, typename RV, typename RV2>
  requires (Op == compare_op::and_op || Op == compare_op::or_op)
auto operator&&(const composite_condition<Op, L, R>& l,
                const leaf_condition<ROp, RC, RV, RV2>& r)
    -> composite_condition<compare_op::and_op,
                           composite_condition<Op, L, R>,
                           leaf_condition<ROp, RC, RV, RV2>> {
  return composite_condition<compare_op::and_op,
                             composite_condition<Op, L, R>,
                             leaf_condition<ROp, RC, RV, RV2>>(l, r);
}

/**
 * @brief AND 演算子 (composite + composite)
 */
template <compare_op LOp, typename LL, typename LR,
          compare_op ROp, typename RL, typename RR>
  requires ((LOp == compare_op::and_op || LOp == compare_op::or_op)
         && (ROp == compare_op::and_op || ROp == compare_op::or_op))
auto operator&&(const composite_condition<LOp, LL, LR>& l,
                const composite_condition<ROp, RL, RR>& r)
    -> composite_condition<compare_op::and_op,
                           composite_condition<LOp, LL, LR>,
                           composite_condition<ROp, RL, RR>> {
  return composite_condition<compare_op::and_op,
                             composite_condition<LOp, LL, LR>,
                             composite_condition<ROp, RL, RR>>(l, r);
}

/**
 * @brief 任意の condition 型 (葉または合成) であることを判定するコンセプト
 *
 * leaf_condition は `using is_leaf_condition_tag = void;` を、
 * composite_condition は `using is_composite_condition_tag = void;` を
 * 持つことでコンセプトが真になる。
 */
template <typename T>
concept any_condition = requires {
  typename T::is_leaf_condition_tag;
} || requires {
  typename T::is_composite_condition_tag;
};

namespace detail {

/**
 * @brief IS NULL / IS NOT NULL など値を持たない条件で使う空の値型
 */
struct empty_value {
  constexpr empty_value() = default;
};

}  // namespace detail

/**
 * @brief T が void なら空の値型に、そうでなければ T 自身に置換する
 */
template <typename T>
using maybe_empty = std::conditional_t<std::is_void_v<T>, detail::empty_value, T>;

/**
 * @brief 葉条件の本体
 */
template <compare_op Op, fixed_string C, typename V, typename V2>
class leaf_condition {
  static_assert(is_leaf_op(Op), "leaf_condition requires a leaf op");

  static constexpr auto column_view = std::string_view(C);

  maybe_empty<V>  value_;
  maybe_empty<V2> value2_;

  public:
  using is_leaf_condition_tag = void;
  static constexpr compare_op op = Op;
  using column_type            = decltype(C);

  constexpr leaf_condition() : value_{}, value2_{} {}
  explicit constexpr leaf_condition(maybe_empty<V> v) : value_(std::move(v)), value2_{} {}
  constexpr leaf_condition(maybe_empty<V> v, maybe_empty<V2> v2) : value_(std::move(v)), value2_(std::move(v2)) {}

  /**
   * @brief プレースホルダ数
   */
  static constexpr auto placeholder_count() -> size_t {
    if constexpr (Op == compare_op::between) {
      return 2;
    } else if constexpr (Op == compare_op::is_null || Op == compare_op::is_not_null) {
      return 0;
    } else if constexpr (Op == compare_op::in_op) {
      return std::tuple_size_v<V>;
    } else {
      return 1;
    }
  }

  /**
   * @brief SQL フラグメントを生成
   */
  auto fragment() const -> std::string {
    if constexpr (Op == compare_op::eq) {
      return std::format("{} = ?", column_view);
    } else if constexpr (Op == compare_op::ne) {
      return std::format("{} != ?", column_view);
    } else if constexpr (Op == compare_op::lt) {
      return std::format("{} < ?", column_view);
    } else if constexpr (Op == compare_op::le) {
      return std::format("{} <= ?", column_view);
    } else if constexpr (Op == compare_op::gt) {
      return std::format("{} > ?", column_view);
    } else if constexpr (Op == compare_op::ge) {
      return std::format("{} >= ?", column_view);
    } else if constexpr (Op == compare_op::like) {
      return std::format("{} LIKE ?", column_view);
    } else if constexpr (Op == compare_op::in_op) {
      return fragment_in_op();
    } else if constexpr (Op == compare_op::between) {
      return std::format("{} BETWEEN ? AND ?", column_view);
    } else if constexpr (Op == compare_op::is_null) {
      return std::format("{} IS NULL", column_view);
    } else if constexpr (Op == compare_op::is_not_null) {
      return std::format("{} IS NOT NULL", column_view);
    } else {
      static_assert(is_leaf_op(Op), "unsupported leaf op");
    }
  }

  /**
   * @brief 値をバインドし次のバインド位置を返す
   */
  auto bind(sqlite3_stmt* stmt, int start_index) const -> int {
    if constexpr (Op == compare_op::is_null || Op == compare_op::is_not_null) {
      return start_index;
    } else if constexpr (Op == compare_op::between) {
      int idx = start_index;
      sqlite_type_traits<V>::bind(stmt, idx, value_);
      sqlite_type_traits<V2>::bind(stmt, idx + 1, value2_);
      return idx + 2;
    } else if constexpr (Op == compare_op::in_op) {
      return bind_in_op(stmt, start_index);
    } else {
      sqlite_type_traits<V>::bind(stmt, start_index, value_);
      return start_index + 1;
    }
  }

  private:
  /**
   * @brief IN 演算子のフラグメント生成 (Op == in_op でのみ実体化)
   */
  template <size_t I = 0, typename Tuple>
  auto append_in_placeholders(std::string& frag, bool& first, Tuple const& t) const -> void {
    if constexpr (I < std::tuple_size_v<Tuple>) {
      frag += std::string(first ? "?" : ", ?");
      first = false;
      append_in_placeholders<I + 1>(frag, first, t);
    }
  }

  auto fragment_in_op() const -> std::string {
    std::string frag  = std::format("{} IN (", column_view);
    bool        first = true;
    append_in_placeholders(frag, first, value_);
    frag += ")";
    return frag;
  }

  /**
   * @brief IN 演算子のバインド (Op == in_op でのみ実体化)
   */
  template <size_t I = 0, typename Tuple>
  auto bind_in_op_impl(sqlite3_stmt* stmt, int& idx, Tuple const& t) const -> void {
    if constexpr (I < std::tuple_size_v<Tuple>) {
      using elem_t = std::remove_cvref_t<std::tuple_element_t<I, Tuple>>;
      sqlite_type_traits<elem_t>::bind(stmt, idx++, std::get<I>(t));
      bind_in_op_impl<I + 1>(stmt, idx, t);
    }
  }

  auto bind_in_op(sqlite3_stmt* stmt, int start_index) const -> int {
    int idx = start_index;
    bind_in_op_impl(stmt, idx, value_);
    return idx;
  }
};

/**
 * @brief where_eq ファクトリ
 */
template <fixed_string C, typename V>
auto where_eq(V v) -> leaf_condition<compare_op::eq, C, std::decay_t<V>> {
  return leaf_condition<compare_op::eq, C, std::decay_t<V>>(std::move(v));
}

template <fixed_string C, typename V>
auto where_ne(V v) -> leaf_condition<compare_op::ne, C, std::decay_t<V>> {
  return leaf_condition<compare_op::ne, C, std::decay_t<V>>(std::move(v));
}

template <fixed_string C, typename V>
auto where_lt(V v) -> leaf_condition<compare_op::lt, C, std::decay_t<V>> {
  return leaf_condition<compare_op::lt, C, std::decay_t<V>>(std::move(v));
}

template <fixed_string C, typename V>
auto where_le(V v) -> leaf_condition<compare_op::le, C, std::decay_t<V>> {
  return leaf_condition<compare_op::le, C, std::decay_t<V>>(std::move(v));
}

template <fixed_string C, typename V>
auto where_gt(V v) -> leaf_condition<compare_op::gt, C, std::decay_t<V>> {
  return leaf_condition<compare_op::gt, C, std::decay_t<V>>(std::move(v));
}

template <fixed_string C, typename V>
auto where_ge(V v) -> leaf_condition<compare_op::ge, C, std::decay_t<V>> {
  return leaf_condition<compare_op::ge, C, std::decay_t<V>>(std::move(v));
}

template <fixed_string C, typename V>
auto where_like(V v) -> leaf_condition<compare_op::like, C, std::decay_t<V>> {
  return leaf_condition<compare_op::like, C, std::decay_t<V>>(std::move(v));
}

/**
 * @brief IN 条件 (可変長、すべての値は同一型)
 */
template <fixed_string C, typename V, typename... Vs>
  requires (std::same_as<V, Vs> && ...)
auto where_in(V v, Vs... vs) -> leaf_condition<compare_op::in_op, C, std::tuple<std::decay_t<V>, std::decay_t<Vs>...>> {
  return leaf_condition<compare_op::in_op, C, std::tuple<std::decay_t<V>, std::decay_t<Vs>...>>(std::tuple<std::decay_t<V>, std::decay_t<Vs>...>(std::move(v), std::move(vs)...));
}

template <fixed_string C, typename V>
auto where_between(V lo, V hi) -> leaf_condition<compare_op::between, C, std::decay_t<V>, std::decay_t<V>> {
  return leaf_condition<compare_op::between, C, std::decay_t<V>, std::decay_t<V>>(std::move(lo), std::move(hi));
}

/**
 * @brief IS NULL
 */
template <fixed_string C>
auto where_is_null() -> leaf_condition<compare_op::is_null, C> {
  return leaf_condition<compare_op::is_null, C>();
}

/**
 * @brief IS NOT NULL
 */
template <fixed_string C>
auto where_is_not_null() -> leaf_condition<compare_op::is_not_null, C> {
  return leaf_condition<compare_op::is_not_null, C>();
}

}  // namespace glz_sql
