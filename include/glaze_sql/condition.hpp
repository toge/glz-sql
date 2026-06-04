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

}  // namespace glz_sql
