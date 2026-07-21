// <Assert.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

/*!
 * \file Assert.hpp
 * \brief Set of macros for SimDB assertions.
 */

/*!
 * \def SIMDB_EXPECT_FALSE
 * \brief A macro for hinting to the compiler a particular condition
 *        should be considered most likely false
 *
 * \code
 * if(SIMDB_EXPECT_FALSE(my_usually_false_condition)) {}
 * \endcode
 */
#define SIMDB_EXPECT_FALSE(x) __builtin_expect(!!(x), false)

/*!
 * \def SIMDB_EXPECT_TRUE
 * \brief A macro for hinting to the compiler a particular condition
 *        should be considered most likely true
 *
 * \code
 * if(SIMDB_EXPECT_TRUE(my_usually_true_condition)) {}
 * \endcode
 */
#define SIMDB_EXPECT_TRUE(x) __builtin_expect(!!(x), true)

#ifndef DO_NOT_DOCUMENT

#define ADD_FILE_INFORMATION(ex, file, line) ex << ": in file: '" << file << "', on line: " << std::dec << line;

#define SIMDB_THROW_EXCEPTION(reason, file, line) \
    simdb::DBException ex(reason);                \
    ADD_FILE_INFORMATION(ex, file, line)          \
    throw ex;

#define SIMDB_ABORT(reason, file, line)   \
    std::stringstream msg(reason);        \
    ADD_FILE_INFORMATION(msg, file, line) \
    std::cerr << msg.str() << std::endl;  \
    std::terminate();

#define simdb_assert1(e)                              \
    if (__builtin_expect(!(e), 0))                    \
    {                                                 \
        SIMDB_THROW_EXCEPTION(#e, __FILE__, __LINE__) \
    }

#define simdb_assert2(e, insertions)                   \
    if (__builtin_expect(!(e), 0))                     \
    {                                                  \
        simdb::DBException ex(std::string(#e) + ": "); \
        ex << insertions;                              \
        ADD_FILE_INFORMATION(ex, __FILE__, __LINE__);  \
        throw ex;                                      \
    }

#define simdb_throw(message)                                       \
    {                                                              \
        std::stringstream msg;                                     \
        msg << message;                                            \
        simdb::DBException ex(std::string("abort: ") + msg.str()); \
        ADD_FILE_INFORMATION(ex, __FILE__, __LINE__);              \
        throw ex;                                                  \
    }

#define simdb_abort1(e)                     \
    if (__builtin_expect(!(e), 0))          \
    {                                       \
        SIMDB_ABORT(#e, __FILE__, __LINE__) \
    }

#define simdb_abort2(e, insertions)                    \
    if (__builtin_expect(!(e), 0))                     \
    {                                                  \
        std::stringstream msg(std::string(#e) + ": "); \
        msg << insertions;                             \
        ADD_FILE_INFORMATION(msg, __FILE__, __LINE__); \
        std::cerr << msg.str() << std::endl;           \
        std::terminate();                              \
    }

#define VA_NARGS_IMPL(_1, _2, _3, _4, _5, N, ...) N
#define VA_NARGS(...) VA_NARGS_IMPL(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define simdb_assert_impl2(count, ...) simdb_assert##count(__VA_ARGS__)
#define simdb_assert_impl(count, ...) simdb_assert_impl2(count, __VA_ARGS__)
#define simdb_abort_impl2(count, ...) simdb_abort##count(__VA_ARGS__)
#define simdb_abort_impl(count, ...) simdb_abort_impl2(count, __VA_ARGS__)

// DO_NOT_DOCUMENT
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvariadic-macros"
/*!
 * \def simdb_assert
 * \brief Simple variadic assertion that will throw a DBException
 *        if the condition fails
 *
 * \throw SpartaException including file and line information if \a e evaluates to
 *        false
 * \note This assertion remains even if compiling with NDEBUG
 *
 * How to use:
 * \code
 *   simdb_assert(condition);
 *   simdb_assert(condition, "My nasty gram");
 *   simdb_assert(condition, "ostream supported message with a value: " << value);
 * \endcode
 *
 * Mental thought "Make sure that condition is true.  If not send message"
 */
#define simdb_assert(...) simdb_assert_impl(VA_NARGS(__VA_ARGS__), __VA_ARGS__)

#pragma GCC diagnostic pop

/*!
 * \def simdb_assert_errno
 * \brief Simple assert macro that throws a simdb_exception with a string
 *        representation of errno
 */
#define simdb_assert_errno(_cond) simdb_assert(_cond, std::string(std::strerror(errno)))

/*!
 * \def simdb_abort
 * \brief Simple variatic assertion that will print a message to std::cerr
 *        and call std::terminate()
 *
 * \details Use instead of simdb_assert() whenever you need an assertion
 *          in a noexcept(true) function (commonly destructors).
 *
 * \note This assertion remains even if compiling with NDEBUG
 *
 * How to use:
 * \code
 *   simdb_abort(condition);
 *   simdb_abort(condition, "My nasty gram");
 *   simdb_abort(condition, "ostream supported message with a value: " << value);
 * \endcode
 *
 * Mental thought "Make sure that condition is true.  If not print message and abort"
 */
#define simdb_abort(...) simdb_abort_impl(VA_NARGS(__VA_ARGS__), __VA_ARGS__)
