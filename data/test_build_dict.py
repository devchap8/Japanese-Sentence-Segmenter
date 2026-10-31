#!/usr/bin/env python3
"""Tests for build_dict.py. Uses inline samples; needs no network or raw data."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import build_dict  # noqa: E402

SAMPLE_XML = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE JMdict [
<!ELEMENT JMdict (entry*)>
<!ENTITY v1 "Ichidan verb">
<!ENTITY vt "transitive verb">
<!ENTITY n "noun (common) (futsuumeishi)">
]>
<JMdict>
<!-- JMdict created: 2026-10-31 -->
<entry>
<ent_seq>1358280</ent_seq>
<k_ele><keb>食べる</keb><ke_pri>ichi1</ke_pri><ke_pri>news1</ke_pri></k_ele>
<k_ele><keb>喰べる</keb></k_ele>
<r_ele><reb>たべる</reb><re_pri>ichi1</re_pri></r_ele>
<sense><pos>&v1;</pos><pos>&vt;</pos><gloss>to eat</gloss><gloss>to dine</gloss></sense>
<sense><gloss>to live on</gloss></sense>
</entry>
<entry>
<ent_seq>2</ent_seq>
<k_ele><keb>甲</keb></k_ele>
<k_ele><keb>乙</keb></k_ele>
<r_ele><reb>こう</reb><re_restr>甲</re_restr></r_ele>
<r_ele><reb>おつ</reb><re_restr>乙</re_restr></r_ele>
<r_ele><reb>コウ</reb><re_nokanji/></r_ele>
<sense><pos>&n;</pos><gloss>salt &amp; pepper; a/b, c</gloss></sense>
</entry>
<entry>
<ent_seq>3</ent_seq>
<r_ele><reb>コーヒー</reb><re_pri>gai1</re_pri></r_ele>
<sense><pos>&n;</pos><gloss>coffee</gloss></sense>
</entry>
<entry>
<ent_seq>4</ent_seq>
<r_ele><reb>なし</reb></r_ele>
<sense><pos>&n;</pos><gloss>no pitch data</gloss></sense>
</entry>
</JMdict>
"""

SAMPLE_ACCENTS = "\n".join(
    [
        "食べる\tたべる\t2",
        "甲\tこう\t1",
        "乙\tおつ\t(副)0,(名)3",
        "コーヒー\t\t3",
        "どこにもない\tどこにもない\t0",
        "malformed line",
    ]
)

EXPECTED_OUTPUT = "\n".join(
    [
        "# header",
        "E\t1358280",
        "K\t食べる\tichi1,news1",
        "K\t喰べる\t",
        "R\tたべる\tichi1\t*\t2",
        "S\tv1,vt\tto eat\tto dine",
        "S\tv1,vt\tto live on",
        "E\t2",
        "K\t甲\t",
        "K\t乙\t",
        "R\tこう\t\t甲\t1",
        "R\tおつ\t\t乙\t0,3",
        "R\tコウ\t\t\t",
        "S\tn\tsalt & pepper; a/b, c",
        "E\t3",
        "R\tコーヒー\tgai1\t*\t3",
        "S\tn\tcoffee",
        "E\t4",
        "R\tなし\t\t*\t",
        "S\tn\tno pitch data",
        "",
    ]
)


class PrepareXmlTest(unittest.TestCase):
    def test_removes_dtd(self):
        self.assertNotIn("ENTITY", build_dict.prepare_xml(SAMPLE_XML))

    def test_rewrites_entities_to_codes(self):
        self.assertEqual(build_dict.prepare_xml("<pos>&v5k-s;</pos>"), "<pos>v5k-s</pos>")

    def test_keeps_predefined_entities(self):
        text = "&amp;&lt;&gt;&quot;&apos;"
        self.assertEqual(build_dict.prepare_xml(text), text)

    def test_reads_date(self):
        self.assertEqual(build_dict.jmdict_date(SAMPLE_XML), "2026-10-31")


class ParseTest(unittest.TestCase):
    def test_pos_carries_forward(self):
        first = build_dict.parse_jmdict(SAMPLE_XML)[0]
        self.assertEqual(first["senses"][1]["pos"], ["v1", "vt"])

    def test_priority_is_per_form(self):
        first = build_dict.parse_jmdict(SAMPLE_XML)[0]
        self.assertEqual(first["kanji"][0]["priorities"], ["ichi1", "news1"])
        self.assertEqual(first["kanji"][1]["priorities"], [])


class AccentTest(unittest.TestCase):
    def test_normalize(self):
        self.assertEqual(build_dict.normalize_accent("2"), ["2"])
        self.assertEqual(build_dict.normalize_accent("0,3"), ["0", "3"])
        self.assertEqual(build_dict.normalize_accent("(副)0,(名)3"), ["0", "3"])
        self.assertEqual(build_dict.normalize_accent("(副)1,(名)1"), ["1"])

    def test_fold_kana(self):
        self.assertEqual(build_dict.fold_kana("コーヒー食べる"), "こーひー食べる")

    def test_duplicate_lines_merge(self):
        accents = build_dict.parse_accents("語\tご\t1\n語\tご\t1,2\n")
        self.assertEqual(accents[("語", "ご")], ["1", "2"])


class BuildTest(unittest.TestCase):
    def setUp(self):
        self.output, self.stats = build_dict.build(
            SAMPLE_XML, SAMPLE_ACCENTS, "# header"
        )

    def test_exact_output(self):
        self.assertEqual(self.output, EXPECTED_OUTPUT)

    def test_stats(self):
        self.assertEqual(self.stats["entries"], 4)
        self.assertEqual(self.stats["longest_key"], 4)
        self.assertEqual(self.stats["entries_with_priority"], 2)
        self.assertEqual(self.stats["readings"], 6)
        self.assertEqual(self.stats["readings_with_pitch"], 4)
        self.assertEqual(
            self.stats["unmatched_accent_keys"], [("どこにもない", "どこにもない")]
        )

    def test_priority_pitch_coverage(self):
        self.assertEqual(self.stats["priority_entries_with_pitch"], 2)

    def test_first_spelling_wins_and_all_spellings_count_as_matched(self):
        xml = """<JMdict><entry><ent_seq>1</ent_seq>
        <k_ele><keb>取り入れ</keb></k_ele><k_ele><keb>取入れ</keb></k_ele>
        <r_ele><reb>とりいれ</reb></r_ele>
        <sense><pos>n</pos><gloss>harvest</gloss></sense>
        </entry></JMdict>"""
        accents = "取り入れ\tとりいれ\t0\n取入れ\tとりいれ\t4\n"
        output, stats = build_dict.build(xml, accents)
        self.assertIn("R\tとりいれ\t\t*\t0\n", output)
        self.assertEqual(stats["unmatched_accent_keys"], [])

    def test_output_is_deterministic(self):
        again, _ = build_dict.build(SAMPLE_XML, SAMPLE_ACCENTS, "# header")
        self.assertEqual(again, self.output)


class CheckedTest(unittest.TestCase):
    def test_rejects_tab_and_newline(self):
        with self.assertRaises(ValueError):
            build_dict.checked("a\tb")
        with self.assertRaises(ValueError):
            build_dict.checked("a\nb")

    def test_comma_only_rejected_in_list_fields(self):
        self.assertEqual(build_dict.checked("a, b"), "a, b")
        with self.assertRaises(ValueError):
            build_dict.joined(["a,b"])


if __name__ == "__main__":
    unittest.main()
