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
class composite_condition;

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

}  // namespace glz_sql
