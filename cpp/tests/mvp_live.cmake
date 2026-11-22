if(NOT DEFINED ENV{ARK_REDIS_PORT} OR "$ENV{ARK_REDIS_PORT}" STREQUAL "")
    message(FATAL_ERROR "ARK_REDIS_PORT must point to a Redis instance dedicated to testing")
endif()

string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" suffix)
set(test_dir "${CMAKE_CURRENT_BINARY_DIR}/mvp-${suffix}")
file(MAKE_DIRECTORY "${test_dir}")
set(ENV{ARK_EPISODIC_DB} "${test_dir}/episodes.db")
set(ENV{ARK_PROCEDURAL_DB} "${test_dir}/procedural.db")
set(ENV{ARK_STATE_PATH} "${test_dir}/STATE.md")
set(ENV{ARK_REDIS_DB} "15")

if(NOT DEFINED goal)
    set(goal "Research Fibonacci algorithms using arXiv:1803.07199, then implement fib(n) in solution.py. Write a private helper _pair(n) which always returns (F(n), F(n+1)). The helper returns (0,1) at n=0. Otherwise call the helper on n//2 for a,b; compute c=a*(2*b-a) and d=a*a+b*b; return (c,d) if n is even or (d,c+d) if odd. The public fib(n) raises ValueError for negative n and returns only _pair(n)[0].")
endif()

function(json_string input output)
    string(REPLACE "\\" "\\\\" value "${input}")
    string(REPLACE "\"" "\\\"" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    set(${output} "\"${value}\"" PARENT_SCOPE)
endfunction()

set(locked_test [=[
import sys
import unittest
from solution import fib

class Contract(unittest.TestCase):
    def test_values(self):
        self.assertEqual(fib(0), 0)
        self.assertEqual(fib(10), 55)
        with self.assertRaises(ValueError):
            fib(-1)

    def test_fast_doubling(self):
        lines = 0
        def trace(frame, event, arg):
            nonlocal lines
            if event == 'line' and frame.f_code.co_filename.endswith('/solution.py'):
                lines += 1
            return trace
        sys.settrace(trace)
        try:
            value = fib(4000)
        finally:
            sys.settrace(None)
        self.assertEqual(value.bit_length(), 2776)
        self.assertLess(lines, 500, 'Use O(log n) fast doubling, not iteration over n')
]=])
json_string("${locked_test}" locked_test_json)
string(JSON locked_files SET "{}" test_contract.py "${locked_test_json}")
file(WRITE "${test_dir}/locked-tests.json" "${locked_files}")

execute_process(COMMAND "${ark}" run "${goal}" "${test_dir}/locked-tests.json"
                WORKING_DIRECTORY "${source_dir}" OUTPUT_VARIABLE result ERROR_VARIABLE error
                RESULT_VARIABLE exit_code TIMEOUT 240)
file(WRITE "${test_dir}/result.json" "${result}")
if(NOT exit_code STREQUAL "0")
    message(FATAL_ERROR "MVP run failed (${exit_code}) at ${test_dir}/result.json: ${error}")
endif()

string(JSON score GET "${result}" quality_score)
if(score STREQUAL "" OR score LESS 0.70)
    message(FATAL_ERROR "MVP score ${score} is below 0.70; see ${test_dir}/result.json")
endif()
string(JSON results GET "${result}" results)
string(JSON count LENGTH "${results}")
math(EXPR last "${count} - 1")
set(reference_found FALSE)
set(code_found FALSE)
foreach(i RANGE 0 "${last}")
    string(JSON key MEMBER "${results}" "${i}")
    string(JSON intent GET "${results}" "${key}" intent)
    if(intent STREQUAL "RESEARCH" OR intent STREQUAL "SEARCH")
        string(JSON hits LENGTH "${results}" "${key}" output citations)
        if(hits GREATER 0)
            math(EXPR citation_last "${hits} - 1")
            foreach(j RANGE 0 "${citation_last}")
                string(JSON cited_id GET "${results}" "${key}" output citations "${j}" arxiv_id)
                if(cited_id MATCHES "^1803\\.07199(v[0-9]+)?$")
                    set(reference_found TRUE)
                endif()
            endforeach()
        endif()
    elseif(intent STREQUAL "CODE" AND NOT code_found)
        string(JSON proposed GET "${results}" "${key}" output files)
        string(JSON solution ERROR_VARIABLE missing GET "${proposed}" solution.py)
        if(missing STREQUAL "NOTFOUND")
            set(code_found TRUE)
            set(files "${proposed}")
        endif()
    endif()
endforeach()
if(NOT code_found OR NOT reference_found)
    message(FATAL_ERROR "Expected source code and the Fibonacci paper citation at ${test_dir}/result.json")
endif()

set(independent_test [=[
import unittest
from solution import fib

class FibonacciTest(unittest.TestCase):
    def test_known_values(self):
        for n, expected in [(0, 0), (1, 1), (10, 55), (100, 354224848179261915075)]:
            self.assertEqual(fib(n), expected)

    def test_negative(self):
        with self.assertRaises(ValueError):
            fib(-1)

    def test_large(self):
        a, b = 0, 1
        for _ in range(2000):
            a, b = b, a + b
        self.assertEqual(fib(2000), a)
]=])
json_string("${independent_test}" independent_test_json)
string(JSON files SET "${files}" test_mvp_independent.py "${independent_test_json}")
set(validation "{}")
string(JSON validation SET "${validation}" files "${files}")
string(JSON validation SET "${validation}" tests true)
file(WRITE "${test_dir}/validation.json" "${validation}")
execute_process(COMMAND "${ark}" sandbox INPUT_FILE "${test_dir}/validation.json"
                WORKING_DIRECTORY "${source_dir}" RESULT_VARIABLE check_result OUTPUT_VARIABLE check_output
                ERROR_VARIABLE check_error TIMEOUT 60)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "Independent Fibonacci tests failed: ${check_output} ${check_error}")
endif()
file(WRITE "${test_dir}/validation-result.json" "${check_output}")

execute_process(COMMAND "${ark}" episodes OUTPUT_VARIABLE episodes RESULT_VARIABLE episode_exit)
string(JSON recorded LENGTH "${episodes}")
if(NOT episode_exit EQUAL 0 OR recorded LESS 3)
    message(FATAL_ERROR "Research, coding and evaluation episodes should persist")
endif()
message(STATUS "MVP score: ${score}; research, independent Docker tests and memory verified in ${test_dir}")
