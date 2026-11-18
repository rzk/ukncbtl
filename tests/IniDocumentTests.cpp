/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/IniDocument.h"
#include <gtest/gtest.h>

TEST(IniDocument, PreservesUnmodifiedTextAndLineEndings)
{
    const std::wstring text = L"; comment\r\n[UKNCBTL]\nKey=Value\r[Other]\nlast";
    EXPECT_EQ(IniDocument(text).Serialize(), text);
}

TEST(IniDocument, ReadsSectionsAndKeysIgnoringAsciiCaseAndWhitespace)
{
    IniDocument document(L"[Other]\nKey=wrong\n[ UKNCBTL ]\n ;Key=comment\n KeY = value = 42 \nEmpty=\n");
    EXPECT_EQ(document.Get(L"ukncbtl", L"key"), L"value = 42");
    EXPECT_EQ(document.Get(L"UKNCBTL", L"empty"), L"");
    EXPECT_FALSE(document.Get(L"missing", L"key"));
    EXPECT_FALSE(document.Get(L"UKNCBTL", L"missing"));
}

TEST(IniDocument, UpdatesFirstMatchingKeyWithoutChangingOtherLines)
{
    IniDocument document(L"; heading\r\n[ukncbtl]\r\nKey = old\r\nKey=duplicate\r\n[other]\r\nx=1");
    ASSERT_TRUE(document.Set(L"UKNCBTL", L"key", L"new"));
    EXPECT_EQ(document.Get(L"ukncbtl", L"KEY"), L"new");
    EXPECT_EQ(document.Serialize(), L"; heading\r\n[ukncbtl]\r\nkey=new\r\nKey=duplicate\r\n[other]\r\nx=1");
}

TEST(IniDocument, InsertsInMatchingSectionAndPreservesFinalLine)
{
    IniDocument document(L"[First]\r\na=1\r\n[Other]\r\nlast=2");
    ASSERT_TRUE(document.Set(L"first", L"b", L"2"));
    EXPECT_EQ(document.Serialize(), L"[First]\r\na=1\r\nb=2\r\n[Other]\r\nlast=2");
    EXPECT_EQ(document.Get(L"FIRST", L"B"), L"2");
}

TEST(IniDocument, AppendsMissingSectionAndHandlesMissingFinalNewline)
{
    IniDocument document(L"[Other]\r\nx=1");
    ASSERT_TRUE(document.Set(L"UKNCBTL", L"Path", L"\u65e5\u672c/\u30c7\u30a3\u30b9\u30af"));
    EXPECT_EQ(document.Serialize(), L"[Other]\r\nx=1\r\n[UKNCBTL]\r\nPath=\u65e5\u672c/\u30c7\u30a3\u30b9\u30af\r\n");
    EXPECT_EQ(document.Get(L"UKNCBTL", L"Path"), L"\u65e5\u672c/\u30c7\u30a3\u30b9\u30af");
    IniDocument empty;
    ASSERT_TRUE(empty.Set(L"a", L"b", L"c"));
    EXPECT_EQ(empty.Serialize(), L"[a]\nb=c\n");
}

TEST(IniDocument, RejectsMalformedInputWithoutChangingDocument)
{
    IniDocument document(L"[section]\nkey=value\n");
    const auto original = document.Serialize();
    EXPECT_FALSE(document.Set(L"", L"key", L"value"));
    EXPECT_FALSE(document.Set(L"bad]section", L"key", L"value"));
    EXPECT_FALSE(document.Set(L"section", L"bad=key", L"value"));
    EXPECT_FALSE(document.Set(L"section", L";comment", L"value"));
    EXPECT_FALSE(document.Set(L"section", L"key", L"a\nb"));
    EXPECT_FALSE(document.Set(L"section", L"key", std::wstring_view(L"a\0b", 3)));
    EXPECT_EQ(document.Serialize(), original);
}

TEST(IniDocument, ReadsQuotedValuesAndWritesLiteralQuotesAndWhitespace)
{
    IniDocument document(L"[section]\nA=\" two words \"\nB='single'\nC=\"\"\n#key=hash\n");
    EXPECT_EQ(document.Get(L"section", L"A"), L" two words ");
    EXPECT_EQ(document.Get(L"section", L"B"), L"single");
    EXPECT_EQ(document.Get(L"section", L"C"), L"");
    EXPECT_EQ(document.Get(L"section", L"#key"), L"hash");
    for (const auto value : {L" value ", L"\"literal\"", L"'literal'", L"\"", L"   "})
    {
        ASSERT_TRUE(document.Set(L"section", L"key", value));
        EXPECT_EQ(IniDocument(document.Serialize()).Get(L"section", L"key"), value);
    }
}

TEST(IniDocument, RemovesOnlyFirstMatchingKeyAndPreservesOtherLines)
{
    IniDocument document(L"[Section]\r\nKey=one\r\nKey=two\r\n[Other]\r\nKey=other");
    EXPECT_TRUE(document.Remove(L"section", L"KEY"));
    EXPECT_EQ(document.Get(L"section", L"key"), L"two");
    EXPECT_EQ(document.Serialize(), L"[Section]\r\nKey=two\r\n[Other]\r\nKey=other");
    EXPECT_FALSE(document.Remove(L"section", L"missing"));
    EXPECT_EQ(document.Get(L"Other", L"Key"), L"other");
}
