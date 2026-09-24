# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CPP CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

set(local_dir "${OUTPUT}.local")
set(user_dir "${OUTPUT}.user")
set(system_dir "${OUTPUT}.system")
file(MAKE_DIRECTORY "${local_dir}" "${user_dir}" "${system_dir}")
file(WRITE "${local_dir}/asset # $.h"
    "#pragma once\n#define ASSET_VALUE 7\n")
file(WRITE "${local_dir}/inactive.h" "#error inactive include selected\n")
file(WRITE "${user_dir}/shared.h" "#define SHARED_VALUE 11\n")
file(WRITE "${system_dir}/shared.h" "#error wrong search order\n")
file(WRITE "${system_dir}/system.h" "#define SYSTEM_VALUE 13\n")
set(source "${local_dir}/main.x")
file(WRITE "${source}"
    "#define TAKE 1\n#if TAKE\n#include \"asset # $.h\"\n"
    "#else\n#include \"inactive.h\"\n#endif\n"
    "#include \"asset # $.h\"\n#include <shared.h>\n"
    "#include <system.h>\n"
    "global i32 answer = ASSET_VALUE + SHARED_VALUE + SYSTEM_VALUE;\n")
file(WRITE "${local_dir}/second.x" "global i32 second = 2;\n")
set(search -I "${user_dir}" -isystem "${system_dir}")

foreach(tool CPP CC)
    execute_process(COMMAND "${${tool}}" -M ${search} "${source}"
        RESULT_VARIABLE status OUTPUT_VARIABLE dep ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${tool} -M failed\n${dep}\n${err}")
    endif()
    foreach(fragment "main.x" "asset\\ \\#\\ $$.h" "inactive.h"
                     "shared.h" "system.h")
        string(FIND "${dep}" "${fragment}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "${tool} dependency output lacks '${fragment}'\n${dep}")
        endif()
    endforeach()
    execute_process(COMMAND "${${tool}}" -MM ${search} "${source}"
        RESULT_VARIABLE status OUTPUT_VARIABLE mm ERROR_VARIABLE err)
    if(NOT status EQUAL 0 OR NOT mm STREQUAL dep)
        message(FATAL_ERROR "${tool} -MM differs from explicit-directory -M\n${mm}\n${err}")
    endif()
endforeach()

execute_process(COMMAND "${CPP}" -MD ${search} "${source}"
    -o "${OUTPUT}.i" -MF "${OUTPUT}.d"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cpp -MD failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.i" expanded)
if(NOT expanded MATCHES "global i32 answer = 7 [+] 11 [+] 13;")
    message(FATAL_ERROR "includes and macros did not expand in order\n${expanded}")
endif()
if(NOT expanded MATCHES "#line [0-9]+ \"[^\"]*main[.]x\"")
    message(FATAL_ERROR "preprocessed input line marker is missing\n${expanded}")
endif()
execute_process(COMMAND "${CC}" -S "${OUTPUT}.i" -o "${OUTPUT}.from-i.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cc did not consume cpp line markers\n${out}\n${err}")
endif()
file(WRITE "${local_dir}/line-meta.h"
    "global i32 header_line = $::source::line;\n"
    "global const u8 header_file[] = $::source::file;\n")
file(WRITE "${local_dir}/line-meta.x"
    "#include \"line-meta.h\"\nglobal i32 input_line = $::source::line;\n")
execute_process(COMMAND "${CPP}" "${local_dir}/line-meta.x"
    -o "${OUTPUT}.line-meta.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "source line macros failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.line-meta.i" marked)
if(NOT marked MATCHES "header_line = 1;" OR
   NOT marked MATCHES "input_line = 2;" OR
   NOT marked MATCHES "header_file\\[\\] = \"[^\"]*line-meta[.]h\";" OR
   NOT marked MATCHES "#line 1 \"[^\"]*line-meta[.]h\"" OR
   NOT marked MATCHES "#line 2 \"[^\"]*line-meta[.]x\"")
    message(FATAL_ERROR "source line macros/markers lost logical locations\n${marked}")
endif()
execute_process(COMMAND "${CC}" -S "${OUTPUT}.line-meta.i"
    -o "${OUTPUT}.line-meta.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "line-marked preprocessed source did not compile\n${out}\n${err}")
endif()
file(WRITE "${local_dir}/line-error.h" "global i32 broken = ;\n")
file(WRITE "${local_dir}/line-error.x" "#include \"line-error.h\"\n")
execute_process(COMMAND "${CPP}" "${local_dir}/line-error.x"
    -o "${OUTPUT}.line-error.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "error fixture did not preprocess\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" -S "${OUTPUT}.line-error.i"
    -o "${OUTPUT}.line-error.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "line-error[.]h:1:[0-9]+: error:")
    message(FATAL_ERROR "preprocessed diagnostic lost included source location\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" -S "${local_dir}/line-error.x"
    -o "${OUTPUT}.line-error-direct.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "line-error[.]h:1:[0-9]+: error:")
    message(FATAL_ERROR "integrated diagnostic lost included source location\n${out}\n${err}")
endif()
file(WRITE "${OUTPUT}.comment-marker.i"
    "/*\n#line 900 \"not-a-marker.x\"\n*/\nglobal i32 comment_safe = 1;\n")
execute_process(COMMAND "${CC}" -S "${OUTPUT}.comment-marker.i"
    -o "${OUTPUT}.comment-marker.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "line-marker spelling inside a comment was consumed\n${out}\n${err}")
endif()
file(WRITE "${OUTPUT}.bad-marker.i" "#line 0 \"bad.x\"\nglobal i32 x = 1;\n")
execute_process(COMMAND "${CC}" -S "${OUTPUT}.bad-marker.i"
    -o "${OUTPUT}.bad-marker.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "malformed preprocessed #line marker")
    message(FATAL_ERROR "invalid line marker was accepted\n${out}\n${err}")
endif()
file(WRITE "${OUTPUT}.negative-marker.i" "#line -1 \"bad.x\"\nglobal i32 x = 1;\n")
execute_process(COMMAND "${CC}" -S "${OUTPUT}.negative-marker.i"
    -o "${OUTPUT}.negative-marker.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "malformed preprocessed #line marker")
    message(FATAL_ERROR "negative line marker was accepted\n${out}\n${err}")
endif()
file(WRITE "${OUTPUT}.bad-unit.i"
    "#$::source::unit unquoted\nglobal i32 x = 1;\n")
execute_process(COMMAND "${CC}" -S "${OUTPUT}.bad-unit.i"
    -o "${OUTPUT}.bad-unit.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "malformed preprocessed source-unit boundary")
    message(FATAL_ERROR "invalid source-unit boundary was accepted\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.d" depfile)
if(NOT depfile STREQUAL dep)
    message(FATAL_ERROR "-MD dependency file differs from -M\n${depfile}\n${dep}")
endif()

execute_process(COMMAND "${CPP}" -MD "-I${user_dir}"
    "-isystem${system_dir}" "${source}" -o "${OUTPUT}.default.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT EXISTS "${OUTPUT}.default.d")
    message(FATAL_ERROR "default -MD depfile or joined search paths failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.default.d" default_dep)
if(NOT default_dep STREQUAL dep)
    message(FATAL_ERROR "default -MD prerequisites differ\n${default_dep}\n${dep}")
endif()

execute_process(COMMAND "${CPP}" -M "${OUTPUT}.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE preprocessed_dep ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT preprocessed_dep MATCHES "preprocessor-dependencies\\.i")
    message(FATAL_ERROR "preprocessed primary dependency failed\n${preprocessed_dep}\n${err}")
endif()
if(preprocessed_dep MATCHES "shared.h")
    message(FATAL_ERROR "preprocessed primary retained old includes\n${preprocessed_dep}")
endif()

execute_process(COMMAND "${CPP}" -MMD ${search} "${source}"
    "${local_dir}/second.x" -MF "${OUTPUT}.multi.d"
    -MQ "target # $" -MT raw_target
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "multi-input -MMD failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.multi.d" multi)
string(FIND "${multi}" "target\\ \\#\\ $$ raw_target:" escaped_targets)
string(REGEX MATCHALL "raw_target:" targets "${multi}")
list(LENGTH targets target_count)
if(escaped_targets EQUAL -1 OR NOT target_count EQUAL 2 OR
   NOT multi MATCHES "second.x")
    message(FATAL_ERROR "expected two dependency rules\n${multi}")
endif()
file(WRITE "${local_dir}/unit-one.x"
    "[[macro]] static $::meta::tokens identity(in $::meta::tokens input) { return input; }\n"
    "static i32 hidden = 11;\nglobal i32 first() { return hidden + identity! { 0 }; }\n")
file(WRITE "${local_dir}/unit-two.x"
    "static i32 hidden = 22;\nglobal i32 second() { return hidden; }\n")
execute_process(COMMAND "${CPP}" "${local_dir}/unit-one.x"
    "${local_dir}/unit-two.x" -o "${OUTPUT}.units.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "multi-input source-unit stream failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.units.i" unit_stream)
string(REGEX MATCHALL "#[$]::source::unit" boundaries "${unit_stream}")
list(LENGTH boundaries boundary_count)
if(NOT boundary_count EQUAL 2)
    message(FATAL_ERROR "multi-input stream lacks two source-unit boundaries\n${unit_stream}")
endif()
execute_process(COMMAND "${CC}" -E "${local_dir}/unit-one.x"
    "${local_dir}/unit-two.x" -o "${OUTPUT}.units.cc.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "integrated multi-input preprocessing failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.units.cc.i" integrated_units)
if(NOT integrated_units STREQUAL unit_stream)
    message(FATAL_ERROR "cpp and cc disagree on source-unit boundaries")
endif()
execute_process(COMMAND "${CC}" -S "${OUTPUT}.units.i"
    -o "${OUTPUT}.units.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "same-spelled statics in separate units collided\n${out}\n${err}")
endif()
file(WRITE "${local_dir}/unit-leak.x"
    "global i32 leak() { return hidden; }\n")
execute_process(COMMAND "${CPP}" "${local_dir}/unit-one.x"
    "${local_dir}/unit-leak.x" -o "${OUTPUT}.unit-leak.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "source-unit leak fixture did not preprocess\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" -S "${OUTPUT}.unit-leak.i"
    -o "${OUTPUT}.unit-leak.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "unit-leak[.]x:1:[0-9]+: error:")
    message(FATAL_ERROR "static declaration leaked across primary units\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.unit-leak.i" leak_stream)
string(REGEX REPLACE "#[$]::source::unit [^\n]*\n" "" merged_stream "${leak_stream}")
file(WRITE "${OUTPUT}.unit-merged.i" "${merged_stream}")
execute_process(COMMAND "${CC}" -S "${OUTPUT}.unit-merged.i"
    -o "${OUTPUT}.unit-merged.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "unit-boundary rejection was not boundary-sensitive\n${out}\n${err}")
endif()
file(WRITE "${local_dir}/unit-repeat.x" "static i32 repeated = 7;\n")
execute_process(COMMAND "${CPP}" "${local_dir}/unit-repeat.x"
    "${local_dir}/unit-repeat.x" -o "${OUTPUT}.unit-repeat.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "repeated primary input did not preprocess\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" -S "${OUTPUT}.unit-repeat.i"
    -o "${OUTPUT}.unit-repeat.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "repeated primary inputs shared a static unit identity\n${out}\n${err}")
endif()

execute_process(COMMAND "${CC}" -MD -S ${search} "${source}"
    -o "${OUTPUT}.s" -MF "${OUTPUT}.cc.d"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cc -MD compile failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.cc.d" ccdep)
if(NOT ccdep MATCHES "preprocessor-dependencies\\.s:")
    message(FATAL_ERROR "cc dependency target ignored -o\n${ccdep}")
endif()

execute_process(COMMAND "${CPP}" -MF "${OUTPUT}.invalid.d" "${source}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "require -M")
    message(FATAL_ERROR "orphan -MF was accepted\n${out}\n${err}")
endif()

execute_process(COMMAND "${CPP}" -MD ${search} "${source}"
    -o "${OUTPUT}.collision.d"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "must be different paths")
    message(FATAL_ERROR "dependency output collision was accepted\n${out}\n${err}")
endif()

# Asset discovery occurs after source macros, before procedural expansion.
set(asset_dir "${OUTPUT}.assets")
file(MAKE_DIRECTORY "${asset_dir}")
file(WRITE "${asset_dir}/payload.bin" "asset bytes")
file(WRITE "${user_dir}/choice.bin" "user choice")
file(WRITE "${system_dir}/choice.bin" "system choice")
set(asset_source "${asset_dir}/main.x")
file(WRITE "${asset_source}"
    "#define PATH \"payload.bin\"\n"
    "static const u8 payload[] = $::embed(PATH);\n"
    "static const u8 again[] = $::embed(\"payload.bin\");\n"
    "static const u8 choice[] = $::embed(\"choice.bin\");\n")
foreach(tool CPP CC)
    execute_process(COMMAND "${${tool}}" -M ${search} "${asset_source}"
        RESULT_VARIABLE status OUTPUT_VARIABLE asset_dep ERROR_VARIABLE err)
    if(NOT status EQUAL 0 OR NOT asset_dep MATCHES "payload.bin" OR
       NOT asset_dep MATCHES "preprocessor-dependencies.user/choice.bin" OR
       asset_dep MATCHES "preprocessor-dependencies.system/choice.bin")
        message(FATAL_ERROR "${tool} asset dependencies failed\n${asset_dep}\n${err}")
    endif()
    string(REGEX MATCHALL "payload.bin" payload_mentions "${asset_dep}")
    list(LENGTH payload_mentions payload_count)
    if(NOT payload_count EQUAL 1)
        message(FATAL_ERROR "${tool} duplicated one asset dependency\n${asset_dep}")
    endif()
endforeach()
foreach(tool CPP CC)
    execute_process(COMMAND "${${tool}}" -E -MD ${search} "${asset_source}"
        -MF "${OUTPUT}.asset.${tool}.d" -o "${OUTPUT}.asset.${tool}.i"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${tool} -MD asset output failed\n${out}\n${err}")
    endif()
    file(READ "${OUTPUT}.asset.${tool}.d" asset_depfile)
    if(NOT asset_depfile MATCHES "payload.bin" OR
       NOT asset_depfile MATCHES "choice.bin")
        message(FATAL_ERROR "${tool} -MD omitted asset prerequisites\n${asset_depfile}")
    endif()
endforeach()
execute_process(COMMAND "${CPP}" -E "${asset_source}" -o "${OUTPUT}.asset.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cpp -E asset preservation failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.asset.i" asset_i)
if(NOT asset_i MATCHES "[$]::embed[(]\"payload.bin\"[)]")
    message(FATAL_ERROR "cpp substituted embedded bytes into source\n${asset_i}")
endif()
execute_process(COMMAND "${CC}" -S ${search} "${asset_source}"
    -o "${OUTPUT}.asset.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES
   "main.x:[0-9]+:[0-9]+: error: [$]::embed byte evaluation")
    message(FATAL_ERROR "pending byte evaluator has an imprecise diagnostic\n${out}\n${err}")
endif()
execute_process(COMMAND "${CPP}" -M "${OUTPUT}.asset.i"
    -I "${asset_dir}" -I "${user_dir}"
    RESULT_VARIABLE status OUTPUT_VARIABLE independent_dep ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT independent_dep MATCHES "payload.bin")
    message(FATAL_ERROR "independent .i asset discovery failed\n${independent_dep}\n${err}")
endif()

file(WRITE "${asset_dir}/templates.x"
    "$::quote { $::embed(\"missing.bin\") };\n"
    "syntax demo : expression { match $::embed(\"missing.bin\"); }\n"
    "$::quote { $::unquote($::embed(\"payload.bin\")) };\n")
execute_process(COMMAND "${CPP}" -M "${asset_dir}/templates.x"
    RESULT_VARIABLE status OUTPUT_VARIABLE template_dep ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT template_dep MATCHES "payload.bin" OR
   template_dep MATCHES "missing.bin")
    message(FATAL_ERROR "quote/pattern embed discovery failed\n${template_dep}\n${err}")
endif()

file(WRITE "${asset_dir}/missing.x"
    "static const u8 value[] = $::embed(\"missing.bin\");\n")
execute_process(COMMAND "${CPP}" -M "${asset_dir}/missing.x"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "missing.x:1:[0-9]+: error: embedded asset")
    message(FATAL_ERROR "missing asset dependency was accepted\n${out}\n${err}")
endif()
execute_process(COMMAND "${CPP}" -E "${asset_dir}/missing.x"
    -o "${OUTPUT}.missing.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cpp -E should preserve an unresolved asset\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" -E "${asset_dir}/missing.x"
    -o "${OUTPUT}.missing-cc.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cc -E should preserve an unresolved asset\n${out}\n${err}")
endif()

file(WRITE "${asset_dir}/independent.i"
    "static const u8 value[] = $::embed(\"payload.bin\");\n")
execute_process(COMMAND "${CPP}" -M "${asset_dir}/independent.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE dep ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT dep MATCHES "payload.bin")
    message(FATAL_ERROR "local .i asset lookup failed\n${dep}\n${err}")
endif()

file(WRITE "${asset_dir}/directory.x"
    "static const u8 value[] = $::embed(\".\");\n")
execute_process(COMMAND "${CPP}" -M "${asset_dir}/directory.x"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "not a regular file")
    message(FATAL_ERROR "nonregular asset was accepted\n${out}\n${err}")
endif()

file(WRITE "${asset_dir}/asset # $.bin" "escaped asset")
file(WRITE "${asset_dir}/escaped.x"
    "static const u8 value[] = $::embed(\"asset # $.bin\");\n")
execute_process(COMMAND "${CPP}" -M "${asset_dir}/escaped.x"
    RESULT_VARIABLE status OUTPUT_VARIABLE dep ERROR_VARIABLE err)
string(FIND "${dep}" "asset\\ \\#\\ $$.bin" escaped_asset)
if(NOT status EQUAL 0 OR escaped_asset EQUAL -1)
    message(FATAL_ERROR "asset Make escaping failed\n${dep}\n${err}")
endif()

file(WRITE "${system_dir}/system-local.bin" "included asset")
file(WRITE "${system_dir}/embed_header.h"
    "static const u8 value[] = $::embed(\"system-local.bin\");\n")
file(WRITE "${asset_dir}/included.x" "#include <embed_header.h>\n")
execute_process(COMMAND "${CPP}" -M "${asset_dir}/included.x"
    -isystem "${system_dir}"
    RESULT_VARIABLE status OUTPUT_VARIABLE dep ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT dep MATCHES "embed_header.h" OR
   NOT dep MATCHES "preprocessor-dependencies.system/system-local.bin")
    message(FATAL_ERROR "included logical source asset lookup failed\n${dep}\n${err}")
endif()
execute_process(COMMAND "${CPP}" "${asset_dir}/included.x"
    -isystem "${system_dir}" -o "${OUTPUT}.included.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "included embed did not preprocess\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" -M "${OUTPUT}.included.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE dep ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT dep MATCHES "system-local.bin" OR
   dep MATCHES "embed_header.h")
    message(FATAL_ERROR "line-marked .i asset lookup/dependencies failed\n${dep}\n${err}")
endif()

file(TO_CMAKE_PATH "${asset_dir}/payload.bin" absolute_asset)
file(WRITE "${asset_dir}/absolute.x"
    "static const u8 value[] = $::embed(\"${absolute_asset}\");\n")
execute_process(COMMAND "${CPP}" -M "${asset_dir}/absolute.x"
    RESULT_VARIABLE status OUTPUT_VARIABLE dep ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT dep MATCHES "payload.bin")
    message(FATAL_ERROR "absolute asset lookup failed\n${dep}\n${err}")
endif()

file(WRITE "${system_dir}/query_header.h"
    "#if !$::has_include(\"system-local.bin\")\n"
    "#error included-file query lost its logical directory\n#endif\n")
file(WRITE "${asset_dir}/queries.x"
    "#if !$::has_include(\"payload.bin\") || !$::has_include(<shared.h>)\n"
    "#error source/include search failed\n#endif\n"
    "#if !$::has_include(<system.h>) || $::has_include(<missing.h>)\n"
    "#error ordered system lookup failed\n#endif\n"
    "#include <query_header.h>\n"
    "global i32 present = $::has_include(\"payload.bin\");\n")
execute_process(COMMAND "${CPP}" -M ${search} "${asset_dir}/queries.x"
    RESULT_VARIABLE status OUTPUT_VARIABLE dep ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR dep MATCHES "payload.bin" OR
   dep MATCHES "system-local.bin" OR NOT dep MATCHES "query_header.h")
    message(FATAL_ERROR "has_include query polluted dependencies\n${dep}\n${err}")
endif()
execute_process(COMMAND "${CPP}" -E ${search} "${asset_dir}/queries.x"
    -o "${OUTPUT}.queries.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "has_include query failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.queries.i" queried)
if(NOT queried MATCHES "global i32 present = 1;")
    message(FATAL_ERROR "ordinary has_include query was not expanded\n${queried}")
endif()
execute_process(COMMAND "${CC}" -E ${search} "${asset_dir}/queries.x"
    -o "${OUTPUT}.queries.cc.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "integrated has_include query failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.queries.cc.i" queried_cc)
if(NOT queried_cc STREQUAL queried)
    message(FATAL_ERROR "cpp and cc disagree on has_include\n${queried_cc}\n${queried}")
endif()
execute_process(COMMAND "${CC}" --print-builtins
    RESULT_VARIABLE status OUTPUT_VARIABLE builtin_list ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT builtin_list MATCHES "[$]::has_include query")
    message(FATAL_ERROR "has_include is absent from builtin inspection\n${builtin_list}\n${err}")
endif()

file(WRITE "${asset_dir}/invalid.x"
    "static const u8 value[] = $::embed(\"one\" \"two\");\n")
execute_process(COMMAND "${CPP}" -M "${asset_dir}/invalid.x"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "exactly one string-literal token")
    message(FATAL_ERROR "invalid embed form was accepted\n${out}\n${err}")
endif()

file(WRITE "${asset_dir}/inactive.x"
    "#if 0\nstatic const u8 value[] = $::embed(\"missing.bin\");\n#endif\n")
execute_process(COMMAND "${CPP}" -M "${asset_dir}/inactive.x"
    RESULT_VARIABLE status OUTPUT_VARIABLE dep ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR dep MATCHES "missing.bin")
    message(FATAL_ERROR "inactive embed became a dependency\n${dep}\n${err}")
endif()
