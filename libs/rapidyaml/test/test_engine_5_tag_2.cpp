#include "./test_lib/test_engine.hpp"

// WARNING: don't use raw string literals -- g++4.8 cannot accept them
// as macro arguments

RYML_DEFINE_TEST_MAIN()

namespace c4 {
namespace yml {


static constexpr const bool singleline = false;


//-----------------------------------------------------------------------------

ENGINE_TEST_ERRLOC(TagTestSuiteU99R_00_0, Location(1,3),
                   "- ,xxx\n")
ENGINE_TEST_ERRLOC(TagTestSuiteU99R_00_1, Location(1,3),
                   "- , xxx\n")
ENGINE_TEST_ERRLOC(TagTestSuiteU99R_0, Location(1,8),
                   "- !!str, xxx\n")
ENGINE_TEST_ERRLOC(TagTestSuiteU99R_1, Location(1,7),
                   "- !str, xxx\n")

ENGINE_TEST(TagTestSuiteU99R_2,
            "[!!str, xxx]"
            ,
            "[!!str ,xxx]"
            ,
            "+STR\n"
            "+DOC\n"
            "+SEQ []\n"
            "=VAL <tag:yaml.org,2002:str> :\n"
            "=VAL :xxx\n"
            "-SEQ\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_seq_val_flow());
    ___(ps.set_val_tag("!!str"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_val_scalar_plain("xxx"));
    ___(ps.end_seq_flow(singleline));
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteU99R_2_1,
            "[!str, xxx]","[!str ,xxx]"
            ,
            "+STR\n"
            "+DOC\n"
            "+SEQ []\n"
            "=VAL <!str> :\n"
            "=VAL :xxx\n"
            "-SEQ\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_seq_val_flow());
    ___(ps.set_val_tag("!str"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_val_scalar_plain("xxx"));
    ___(ps.end_seq_flow(singleline));
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteU99R_3,
            "{!!str, xxx}"
            ,
            "{!!str : ,xxx: }"
            ,
            "+STR\n"
            "+DOC\n"
            "+MAP {}\n"
            "=VAL <tag:yaml.org,2002:str> :\n"
            "=VAL :\n"
            "=VAL :xxx\n"
            "=VAL :\n"
            "-MAP\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_map_val_flow());
    ___(ps.set_key_tag("!!str"));
    ___(ps.set_key_scalar_plain_empty());
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_key_scalar_plain("xxx"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_map_flow(singleline));
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteU99R_3_1,
            "{!str, xxx}"
            ,
            "{!str : ,xxx: }"
            ,
            "+STR\n"
            "+DOC\n"
            "+MAP {}\n"
            "=VAL <!str> :\n"
            "=VAL :\n"
            "=VAL :xxx\n"
            "=VAL :\n"
            "-MAP\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_map_val_flow());
    ___(ps.set_key_tag("!str"));
    ___(ps.set_key_scalar_plain_empty());
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_key_scalar_plain("xxx"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_map_flow(singleline));
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteWZ62_0_0_0,
            "{foo: !!str , !!str : bar}"
            ,
            "{foo: !!str ,!!str : bar}"
            ,
            "+STR\n"
            "+DOC\n"
            "+MAP {}\n"
            "=VAL :foo\n"
            "=VAL <tag:yaml.org,2002:str> :\n"
            "=VAL <tag:yaml.org,2002:str> :\n"
            "=VAL :bar\n"
            "-MAP\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_map_val_flow());
    ___(ps.set_key_scalar_plain("foo"));
    ___(ps.set_val_tag("!!str"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_key_tag("!!str"));
    ___(ps.set_key_scalar_plain_empty());
    ___(ps.set_val_scalar_plain("bar"));
    ___(ps.end_map_flow(singleline));
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteWZ62_0_0_1,
            "foo: !!str\n!!str : bar\n"
            ,
            "foo: !!str \n!!str : bar\n"
            ,
            "+STR\n"
            "+DOC\n"
            "+MAP\n"
            "=VAL :foo\n"
            "=VAL <tag:yaml.org,2002:str> :\n"
            "=VAL <tag:yaml.org,2002:str> :\n"
            "=VAL :bar\n"
            "-MAP\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_map_val_block());
    ___(ps.set_key_scalar_plain("foo"));
    ___(ps.set_val_tag("!!str"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_key_tag("!!str"));
    ___(ps.set_key_scalar_plain_empty());
    ___(ps.set_val_scalar_plain("bar"));
    ___(ps.end_map_block());
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteWZ62_0_1_0,
            "{foo: !str , !str : bar}"
            ,
            "{foo: !str ,!str : bar}"
            ,
            "+STR\n"
            "+DOC\n"
            "+MAP {}\n"
            "=VAL :foo\n"
            "=VAL <!str> :\n"
            "=VAL <!str> :\n"
            "=VAL :bar\n"
            "-MAP\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_map_val_flow());
    ___(ps.set_key_scalar_plain("foo"));
    ___(ps.set_val_tag("!str"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_key_tag("!str"));
    ___(ps.set_key_scalar_plain_empty());
    ___(ps.set_val_scalar_plain("bar"));
    ___(ps.end_map_flow(singleline));
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteWZ62_0_1_1,
            "foo: !str\n!str : bar\n"
            ,
            "foo: !str \n!str : bar\n"
            ,
            "+STR\n"
            "+DOC\n"
            "+MAP\n"
            "=VAL :foo\n"
            "=VAL <!str> :\n"
            "=VAL <!str> :\n"
            "=VAL :bar\n"
            "-MAP\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_map_val_block());
    ___(ps.set_key_scalar_plain("foo"));
    ___(ps.set_val_tag("!str"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_key_tag("!str"));
    ___(ps.set_key_scalar_plain_empty());
    ___(ps.set_val_scalar_plain("bar"));
    ___(ps.end_map_block());
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteWZ62_1_0_0,
            "{foo: !!str, !!str: bar}"
            ,
            "{foo: !!str ,!!str: bar: }"
            ,
            "+STR\n"
            "+DOC\n"
            "+MAP {}\n"
            "=VAL :foo\n"
            "=VAL <tag:yaml.org,2002:str> :\n"
            "=VAL <tag:yaml.org,2002:str:> :bar\n"
            "=VAL :\n"
            "-MAP\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_map_val_flow());
    ___(ps.set_key_scalar_plain("foo"));
    ___(ps.set_val_tag("!!str"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_key_tag("!!str:"));
    ___(ps.set_key_scalar_plain("bar"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_map_block());
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteWZ62_1_0_1,
            "foo: !!str\n!!str: bar:\n"
            ,
            "foo: !!str \n!!str: bar: \n"
            ,
            "+STR\n"
            "+DOC\n"
            "+MAP\n"
            "=VAL :foo\n"
            "=VAL <tag:yaml.org,2002:str> :\n"
            "=VAL <tag:yaml.org,2002:str:> :bar\n"
            "=VAL :\n"
            "-MAP\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_map_val_block());
    ___(ps.set_key_scalar_plain("foo"));
    ___(ps.set_val_tag("!!str"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_key_tag("!!str:"));
    ___(ps.set_key_scalar_plain("bar"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_map_block());
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteWZ62_1_1_0,
            "{foo: !str, !str: bar}"
            ,
            "{foo: !str ,!str: bar: }"
            ,
            "+STR\n"
            "+DOC\n"
            "+MAP {}\n"
            "=VAL :foo\n"
            "=VAL <!str> :\n"
            "=VAL <!str:> :bar\n"
            "=VAL :\n"
            "-MAP\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_map_val_flow());
    ___(ps.set_key_scalar_plain("foo"));
    ___(ps.set_val_tag("!str"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_key_tag("!str:"));
    ___(ps.set_key_scalar_plain("bar"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_map_flow(singleline));
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteWZ62_1_1_1,
            "foo: !str\n"
            "!str: bar:\n"
            ,
            "foo: !str \n"
            "!str: bar: \n"
            ,
            "+STR\n"
            "+DOC\n"
            "+MAP\n"
            "=VAL :foo\n"
            "=VAL <!str> :\n"
            "=VAL <!str:> :bar\n"
            "=VAL :\n"
            "-MAP\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_map_val_block());
    ___(ps.set_key_scalar_plain("foo"));
    ___(ps.set_val_tag("!str"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.add_sibling());
    ___(ps.set_key_tag("!str:"));
    ___(ps.set_key_scalar_plain("bar"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_map_block());
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST_ERRLOC(TagTestSuiteLHL4_0, Location(2,1),
                   "---\n"
                   "!invalid{}tag scalar\n")

ENGINE_TEST_ERRLOC(TagTestSuiteLHL4_1, Location(2,1),
                   "---\n"
                   "!!invalid{}tag scalar\n")

ENGINE_TEST(TagTestSuiteUGM3,
            "--- !<tag:clarkevans.com,2002:invoice>\n"
            "invoice: 34843\n"
            "date: 2001-01-23\n"
            ,
            "+STR\n"
            "+DOC ---\n"
            "+MAP <tag:clarkevans.com,2002:invoice>\n"
            "=VAL :invoice\n"
            "=VAL :34843\n"
            "=VAL :date\n"
            "=VAL :2001-01-23\n"
            "-MAP\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc_expl());
    ___(ps.set_val_tag("!<tag:clarkevans.com,2002:invoice>"));
    ___(ps.begin_map_val_block());
    ___(ps.set_key_scalar_plain("invoice"));
    ___(ps.set_val_scalar_plain("34843"));
    ___(ps.add_sibling());
    ___(ps.set_key_scalar_plain("date"));
    ___(ps.set_val_scalar_plain("2001-01-23"));
    ___(ps.end_map_block());
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuiteUKK6_02_0,
            "!"
            ,
            "+STR\n"
            "+DOC\n"
            "=VAL <!> :\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.set_val_tag("!"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuite6WLZ_0,
            "--- !foo \"bar\"\n"
            "...\n"
            "%TAG ! tag:example.com,2000:app/\n"
            "--- !foo \"bar\"\n"
            ,
            "+STR\n"
            "+DOC ---\n"
            "=VAL <!foo> \"bar\n"
            "-DOC ...\n"
            "+DOC ---\n"
            "=VAL <tag:example.com,2000:app/foo> \"bar\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc_expl());
    ___(ps.set_val_tag("!foo"));
    ___(ps.set_val_scalar_dquoted("bar"));
    ___(ps.end_doc_expl());
    ___(ps.add_directive_tag("!", "tag:example.com,2000:app/"));
    ___(ps.begin_doc_expl());
    ___(ps.set_val_tag("!foo"));
    ___(ps.set_val_scalar_dquoted("bar"));
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagTestSuite6WLZ_1,
            "--- !foo \"bar\"\n"
            "...\n"
            "%TAG ! tag:example.com,2000:app/\n"
            "--- !<tag:example.com,2000:app/foo> \"bar\"\n"
            ,
            "+STR\n"
            "+DOC ---\n"
            "=VAL <!foo> \"bar\n"
            "-DOC ...\n"
            "+DOC ---\n"
            "=VAL <tag:example.com,2000:app/foo> \"bar\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc_expl());
    ___(ps.set_val_tag("!foo"));
    ___(ps.set_val_scalar_dquoted("bar"));
    ___(ps.end_doc_expl());
    ___(ps.add_directive_tag("!", "tag:example.com,2000:app/"));
    ___(ps.begin_doc_expl());
    ___(ps.set_val_tag("<tag:example.com,2000:app/foo>"));
    ___(ps.set_val_scalar_dquoted("bar"));
    ___(ps.end_doc());
    ___(ps.end_stream());
}


//-----------------------------------------------------------------------------

ENGINE_TEST_ERRTY(DirectiveTestSuite9MMA_YAML, ExpectedErrorType::err_parse,
                  "%YAML 1.2")

ENGINE_TEST_ERRTY(DirectiveTestSuite9MMA_TAG, ExpectedErrorType::err_parse,
                "%TAG ! tag:example.com,2000:app/\n")

ENGINE_TEST_ERR(DirectiveTestSuiteMUS6_00,
                "%YAML 1.1#...\n"
                "---\n")
ENGINE_TEST(DirectiveTestSuiteMUS6_01,
            "%YAML 1.1 #...\n"
             "---\n"
            ,
             "---\n"
            ,
            "+STR\n"
            "+DOC ---\n"
            "=VAL :\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.add_directive_yaml("1.1"));
    ___(ps.begin_doc_expl());
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_doc());
    ___(ps.end_stream());
}
ENGINE_TEST(DirectiveTestSuiteMUS6,
            "%YAM 1.1\n" // ignored
            "---\n"
            ,
            "---\n"
            ,
            "+STR\n"
            "+DOC ---\n"
            "=VAL :\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc_expl());
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST_ERR(DirectiveTestSuite9HCY,
                "!foo \"bar\"\n"
                "%TAG ! tag:example.com,2000:app/\n"
                "---\n"
                "!foo \"bar\"\n")

ENGINE_TEST(DirectiveMultipleYAML_W4TN,
            ""
            "%YAML 1.2\n"
            "---\n"
            "foo\n"
            "...\n"
            "%YAML 1.2\n"
            "---\n"
            "# Empty\n"
            "...\n"
            ""
            ,
            "--- foo\n"
            "---\n"
            ""
            ,
            "+STR\n"
            "+DOC ---\n"
            "=VAL :foo\n"
            "-DOC ...\n"
            "+DOC ---\n"
            "=VAL :\n"
            "-DOC ...\n"
            "-STR\n"
            "")
{
    ___(ps.begin_stream());
    ___(ps.add_directive_yaml("1.2"));
    ___(ps.begin_doc_expl());
    ___(ps.set_val_scalar_plain("foo"));
    ___(ps.end_doc_expl());
    ___(ps.add_directive_yaml("1.2"));
    ___(ps.begin_doc_expl());
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_doc_expl());
    ___(ps.end_stream());
}

ENGINE_TEST_ERR(DirectiveMultipleYAML_0,
                "%YAML 1.2\n"
                "...\n"
                "%YAML 1.2\n"
                "---\n"
                "bar")

ENGINE_TEST_ERR(DirectiveMultipleYAML_1,
                "%YAML 1.1\n"
                "...\n"
                "%YAML 1.2\n"
                "---\n"
                "bar")

ENGINE_TEST_ERR(DirectiveMultipleYAML_2,
                "%YAML 1.2\n"
                "%YAML 1.2\n"
                "---\n"
                "bar")

ENGINE_TEST_ERR(DirectiveMultipleYAML_3,
                "%YAML 1.1\n"
                "%YAML 1.2\n"
                "---\n"
                "bar")

ENGINE_TEST_ERRTY(DirectiveWrong, ExpectedErrorType::err_any,
                  "--- !foo \"ba\"\n"
                  "...\n"
                  "%TAG ,,,,,\n"
                  "--- !foo \"bar\"\n")

ENGINE_TEST_ERRTY(DirectiveTooMany, ExpectedErrorType::err_any,
                  "--- !foo \"bar\"\n"
                  "...\n"
                  "%TAG ! tag:example.com,2000:app/\n"
                  "--- !foo \"bar\"\n"
                  "...\n"
                  "%TAG ! tag:example.com,2001:app/\n"
                  "...\n"
                  "--- !foo \"bar\"\n"
                  "...\n"
                  "%TAG ! tag:example.com,2002:app/\n"
                  "--- !foo \"bar\"\n"
                  "...\n"
                  "%TAG ! tag:example.com,2003:app/\n"
                  "--- !foo \"bar\"\n"
                  "...\n"
                  "%TAG ! tag:example.com,2004:app/\n"
                  "--- !foo \"bar\"\n")


//-----------------------------------------------------------------------------

ENGINE_TEST(TagEmptySeq0,
            "!!seq []\n"
            ,
            "!!seq []"
            ,
            "+STR\n+DOC\n+SEQ [] <tag:yaml.org,2002:seq>\n-SEQ\n-DOC\n-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.set_val_tag("!!seq"));
    ___(ps.begin_seq_val_flow());
    ___(ps.end_seq_flow(singleline));
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagEmptySeq1,
            ""
            "- !!seq []\n"
            ""
            ,
            "+STR\n+DOC\n+SEQ\n+SEQ [] <tag:yaml.org,2002:seq>\n-SEQ\n-SEQ\n-DOC\n-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_seq_val_block());
    ___(ps.set_val_tag("!!seq"));
    ___(ps.begin_seq_val_flow());
    ___(ps.end_seq_flow(singleline));
    ___(ps.end_seq_block());
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagEmptySeq2,
            ""
            "!!seq\n"
            "- !!seq []\n"
            ""
            ,
            "+STR\n+DOC\n+SEQ <tag:yaml.org,2002:seq>\n+SEQ [] <tag:yaml.org,2002:seq>\n-SEQ\n-SEQ\n-DOC\n-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.set_val_tag("!!seq"));
    ___(ps.begin_seq_val_block());
    ___(ps.set_val_tag("!!seq"));
    ___(ps.begin_seq_val_flow());
    ___(ps.end_seq_flow(singleline));
    ___(ps.end_seq_block());
    ___(ps.end_doc());
    ___(ps.end_stream());
}


//-----------------------------------------------------------------------------

ENGINE_TEST(TagEmptyMap0,
            "!!map {}\n"
            ,
            "!!map {}"
            ,
            "+STR\n+DOC\n+MAP {} <tag:yaml.org,2002:map>\n-MAP\n-DOC\n-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.set_val_tag("!!map"));
    ___(ps.begin_map_val_flow());
    ___(ps.end_map_flow(singleline));
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagEmptyMap1,
            ""
            "- !!map {}\n"
            ""
            ,
            "+STR\n+DOC\n+SEQ\n+MAP {} <tag:yaml.org,2002:map>\n-MAP\n-SEQ\n-DOC\n-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.begin_seq_val_block());
    ___(ps.set_val_tag("!!map"));
    ___(ps.begin_map_val_flow());
    ___(ps.end_map_flow(singleline));
    ___(ps.end_seq_block());
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagEmptyMap2,
            ""
            "!!seq\n"
            "- !!map {}\n"
            ""
            ,
            "+STR\n+DOC\n+SEQ <tag:yaml.org,2002:seq>\n+MAP {} <tag:yaml.org,2002:map>\n-MAP\n-SEQ\n-DOC\n-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.set_val_tag("!!seq"));
    ___(ps.begin_seq_val_block());
    ___(ps.set_val_tag("!!map"));
    ___(ps.begin_map_val_flow());
    ___(ps.end_map_flow(singleline));
    ___(ps.end_seq_block());
    ___(ps.end_doc());
    ___(ps.end_stream());
}


//-----------------------------------------------------------------------------

ENGINE_TEST(TagYs0,
            ""
            "!yamlscript/v0/bare\n"
            "--- !code\n"
            "--- !data\n"
            "--- !code\n"
            "--- !data\n"
            ""
            ,
            ""
            "--- !yamlscript/v0/bare\n"
            "--- !code\n"
            "--- !data\n"
            "--- !code\n"
            "--- !data\n"
            ""
            ,
            "+STR\n"
            "+DOC\n"
            "=VAL <!yamlscript/v0/bare> :\n"
            "-DOC\n"
            "+DOC ---\n"
            "=VAL <!code> :\n"
            "-DOC\n"
            "+DOC ---\n"
            "=VAL <!data> :\n"
            "-DOC\n"
            "+DOC ---\n"
            "=VAL <!code> :\n"
            "-DOC\n"
            "+DOC ---\n"
            "=VAL <!data> :\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.set_val_tag("!yamlscript/v0/bare"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_doc());
    ___(ps.begin_doc_expl());
    ___(ps.set_val_tag("!code"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_doc());
    ___(ps.begin_doc_expl());
    ___(ps.set_val_tag("!data"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_doc());
    ___(ps.begin_doc_expl());
    ___(ps.set_val_tag("!code"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_doc());
    ___(ps.begin_doc_expl());
    ___(ps.set_val_tag("!data"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_doc());
    ___(ps.end_stream());
}

ENGINE_TEST(TagYs1,
            ""
            "!yamlscript/v0/bare\n"
            "--- !code\n"
            "42\n"
            ""
            ,
            ""
            "--- !yamlscript/v0/bare\n"
            "--- !code 42\n"
            ""
            ,
            "+STR\n"
            "+DOC\n"
            "=VAL <!yamlscript/v0/bare> :\n"
            "-DOC\n"
            "+DOC ---\n"
            "=VAL <!code> :42\n"
            "-DOC\n"
            "-STR\n")
{
    ___(ps.begin_stream());
    ___(ps.begin_doc());
    ___(ps.set_val_tag("!yamlscript/v0/bare"));
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_doc());
    ___(ps.begin_doc_expl());
    ___(ps.set_val_tag("!code"));
    ___(ps.set_val_scalar_plain("42"));
    ___(ps.end_doc());
    ___(ps.end_stream());
}


//-----------------------------------------------------------------------------

ENGINE_TEST_ERRLOC(DirectiveNotAtLineBeginningYamlSpace, Location(1, 2), " %YAML 1.2\n---\n")
ENGINE_TEST_ERRLOC(DirectiveNotAtLineBeginningYamlTab, Location(1, 2), "\t%YAML 1.2\n---\n")
ENGINE_TEST_ERRLOC(DirectiveNotAtLineBeginningOtherTab, Location(1, 2), "\t%FOO\n---\n")
ENGINE_TEST_ERRLOC(DirectiveNotAtLineBeginningTagSpace, Location(1, 2), " %TAG !yaml! tag:yaml.org,2002:\n---\n")
ENGINE_TEST_ERRLOC(DirectiveNotAtLineBeginningTagTab, Location(1, 2), "\t%TAG !yaml! tag:yaml.org,2002:\n---\n")

ENGINE_TEST_ERRLOC(DirectiveYamlBad00, Location(1, 1), "%YAML\n---\n")
ENGINE_TEST_ERRLOC(DirectiveYamlBad01, Location(1, 1), "%YAML    \n---\n")
ENGINE_TEST_ERRLOC(DirectiveYamlBad03, Location(1, 1), "%YAML aa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveYamlBad04, Location(1, 1), "%YAML\taa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveYamlBad05, Location(1, 1), "%YAML a.a\n---\n")
ENGINE_TEST_ERRLOC(DirectiveYamlBad06, Location(1, 1), "%YAML\ta.a\n---\n")
ENGINE_TEST_ERRLOC(DirectiveYamlBad07, Location(1, 9), "%YAML 1.a\n---\n")
ENGINE_TEST_ERRLOC(DirectiveYamlBad08, Location(1, 9), "%YAML\t1.a\n---\n")
ENGINE_TEST_ERRLOC(DirectiveYamlBad09, Location(1, 1), "%YAML a.1\n---\n")
ENGINE_TEST_ERRLOC(DirectiveYamlBad10, Location(1, 1), "%YAML\ta.1\n---\n")
ENGINE_TEST_ERRLOC(DirectiveYamlBad11, Location(1, 11), "%YAML 1.2 aa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveYamlBad12, Location(1, 11), "%YAML 1.2\taa\n---\n")

ENGINE_TEST_ERRLOC(DirectiveTagBad00, Location(1, 1), "%TAG\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad01, Location(1, 1), "%TAG    \n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad04, Location(1, 1), "%TAG aa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad05, Location(1, 1), "%TAG\taa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad06, Location(1, 1), "%TAG !\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad07, Location(1, 1), "%TAG\t!\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad08, Location(1, 1), "%TAG !!\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad09, Location(1, 1), "%TAG\t!!\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad10, Location(1, 1), "%TAG !a\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad11, Location(1, 1), "%TAG\t!a\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad12, Location(1, 1), "%TAG !a!\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad13, Location(1, 1), "%TAG\t!a!\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad14, Location(1, 34), "%TAG ! tag:example.com,2000:app/ aa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad15, Location(1, 34), "%TAG\t! tag:example.com,2000:app/ aa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad16, Location(1, 35), "%TAG !! tag:example.com,2000:app/ aa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad17, Location(1, 35), "%TAG\t!! tag:example.com,2000:app/ aa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad18, Location(1, 1), "%TAG !a tag:example.com,2000:app/ aa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad19, Location(1, 1), "%TAG\t!a tag:example.com,2000:app/ aa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad20, Location(1, 36), "%TAG !a! tag:example.com,2000:app/ aa\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad21, Location(1, 36), "%TAG\t!a! tag:example.com,2000:app0 aa/\n---\n")

ENGINE_TEST_ERRLOC(DirectiveTagBad22, Location(1, 1), "%TAG !A-UPPERCASE-now-bad:$! tag:example.com,2000:app0/\n---\n")
ENGINE_TEST_ERRLOC(DirectiveTagBad23, Location(1, 1), "%TAG !A-UPPERCASE-now-bad:$! tag:example.com,2000:app0/\n---\n")


template<class Ps>
static void foo(Ps &ps)
{
    ___(ps.begin_stream());
    ___(ps.begin_doc_expl());
    ___(ps.set_val_scalar_plain_empty());
    ___(ps.end_doc());
    ___(ps.end_stream());
}
#define DIRECTIVE_TEST(name, directive) \
    ENGINE_TEST(name, NO_COMPARE_EMITTED,                               \
                directive "\n"                                          \
                "---\n"                                                 \
                ,                                                       \
                "---\n"                                                 \
                ,                                                       \
                "+STR\n"                                                \
                "+DOC ---\n"                                            \
                "=VAL :\n"                                              \
                "-DOC\n"                                                \
                "-STR\n")                                               \
    {                                                                   \
        foo(ps);                                                        \
    }

DIRECTIVE_TEST(DirectiveYamlOk0, "%YAML 1.2  #comm")
DIRECTIVE_TEST(DirectiveYamlOk1, "%YAML 1.2 #comm")
DIRECTIVE_TEST(DirectiveYamlOk2, "%YAML\t1.2\t#comm")
DIRECTIVE_TEST(DirectiveYamlOk3, "%YAML  1.2  #comm")
DIRECTIVE_TEST(DirectiveYamlOk4, "%YAML\t\t1.2\t\t#comm")
DIRECTIVE_TEST(DirectiveYamlOk5, "%YAML  \t  1.2  \t #comm")
DIRECTIVE_TEST(DirectiveYamlOk6, "%YAM  \t  1.2  \t #comm")
DIRECTIVE_TEST(DirectiveYamlOk7, "%YAMLL  \t  1.2  \t #comm")

DIRECTIVE_TEST(DirectiveTagOk14, "%TAG ! tag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk15, "%TAG\t!\ttag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk16, "%TAG !! tag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk17, "%TAG\t!!\ttag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk20, "%TAG !a! tag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk21, "%TAG\t!a!\ttag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk22, "%TAG  !a!  tag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk23, "%TAG\t\t!a!\t\ttag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk24, "%TAG \t !a! \t tag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk25, "%TAG\t \t!a!\t \ttag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk26, "%TA \t !a! \t tag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk28, "%TAGG\t \t!a!\t \ttag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk29, "%TAG !Ab-09! tag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk30, "%TAG\t!Ab-09!\ttag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk31, "%TAG  !Ab-09!  tag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk32, "%TAG\t\t!Ab-09!\t\ttag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk33, "%TAG \t !Ab-09! \t tag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk34, "%TAG\t \t!Ab-09!\t \ttag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk35, "%TA \t !Ab-09! \t tag:example.com,2000:app/")
DIRECTIVE_TEST(DirectiveTagOk36, "%TAGG\t \t!Ab-09!\t \ttag:example.com,2000:app/")

} // namespace yml
} // namespace c4


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

// this is needed to use the test case library

#ifndef RYML_SINGLE_HEADER
#include "c4/substr.hpp"
#endif

namespace c4 {
namespace yml {
struct Case;
Case const* get_case(csubstr /*name*/)
{
    return nullptr;
}
} // namespace yml
} // namespace c4
