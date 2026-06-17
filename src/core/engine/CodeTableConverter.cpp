// VKey - Code Table Converter Implementation
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
// Dual-licensed: AGPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.
//
// Mapping tables extracted from OpenKey Vietnamese.cpp _codeTable[0..4].
// Each map: Unicode codepoint → encoded 16-bit value.
// For 2-byte encodings: LOBYTE = first output char, HIBYTE = second (tone/mark).
//
// Note: String-returning functions are marked noexcept by project convention (std::bad_alloc terminates).

#include "CodeTableConverter.h"
#include "VietnameseTables.h"
#include <unordered_map>
#include <cwctype>

namespace NextKey {

// ═══════════════════════════════════════════════════════════
// Mapping tables: Unicode → encoded value
//
// Built by pairing _codeTable[0][position] with _codeTable[N][position]
// from OpenKey's Vietnamese.cpp lines 413-492.
//
// Format for 2-byte encodings (VNI, Compound, CP1258):
//   value > 0xFF → LOBYTE is first char, HIBYTE is second char
//   value <= 0xFF → single char output
// ═══════════════════════════════════════════════════════════

// Map type uses uint32_t to avoid MSVC C4244 narrowing warnings
// (hex literals are int, wchar_t/uint16_t would trigger narrowing)
using CodeMap = std::unordered_map<uint32_t, uint32_t>;

// Forward maps are wrapped in Meyers-singleton accessors (function-local statics)
// rather than eager namespace-scope objects: std::unordered_map has a non-trivial
// ctor, so a namespace-scope instance would heap-allocate ~96 nodes per map during
// CRT startup, unconditionally. The convert feature is on-demand only (ConvertChar
// runs only when codeTable != Unicode, never on the per-keystroke hot path), so the
// majority Unicode-output user should never pay for these. Lazy init defers the
// allocation to first use. Mirrors the lazy reverse-map pattern below.

// TCVN3 (ABC) — always 1 byte output per Vietnamese character
static const CodeMap& GetTcvn3Map() {
    static const CodeMap kTcvn3Map = {
    // KEY_A: Â, â, Ă, ă, Á, á, À, à, Ả, ả, Ã, ã, Ạ, ạ
    {0x00C2, 0xA2}, {0x00E2, 0xA9}, {0x0102, 0xA1}, {0x0103, 0xA8},
    {0x00C1, 0xB8}, {0x00E1, 0xB8}, {0x00C0, 0xB5}, {0x00E0, 0xB5},
    {0x1EA2, 0xB6}, {0x1EA3, 0xB6}, {0x00C3, 0xB7}, {0x00E3, 0xB7},
    {0x1EA0, 0xB9}, {0x1EA1, 0xB9},
    // KEY_O: Ô, ô, Ơ, ơ, Ó, ó, Ò, ò, Ỏ, ỏ, Õ, õ, Ọ, ọ
    {0x00D4, 0xA4}, {0x00F4, 0xAB}, {0x01A0, 0xA5}, {0x01A1, 0xAC},
    {0x00D3, 0xE3}, {0x00F3, 0xE3}, {0x00D2, 0xDF}, {0x00F2, 0xDF},
    {0x1ECE, 0xE1}, {0x1ECF, 0xE1}, {0x00D5, 0xE2}, {0x00F5, 0xE2},
    {0x1ECC, 0xE4}, {0x1ECD, 0xE4},
    // KEY_U: (no Û), Ư, ư, Ú, ú, Ù, ù, Ủ, ủ, Ũ, ũ, Ụ, ụ
    {0x01AF, 0xA6}, {0x01B0, 0xAD},
    {0x00DA, 0xF3}, {0x00FA, 0xF3}, {0x00D9, 0xEF}, {0x00F9, 0xEF},
    {0x1EE6, 0xF1}, {0x1EE7, 0xF1}, {0x0168, 0xF2}, {0x0169, 0xF2},
    {0x1EE4, 0xF4}, {0x1EE5, 0xF4},
    // KEY_E: Ê, ê, É, é, È, è, Ẻ, ẻ, Ẽ, ẽ, Ẹ, ẹ
    {0x00CA, 0xA3}, {0x00EA, 0xAA},
    {0x00C9, 0xD0}, {0x00E9, 0xD0}, {0x00C8, 0xCC}, {0x00E8, 0xCC},
    {0x1EBA, 0xCE}, {0x1EBB, 0xCE}, {0x1EBC, 0xCF}, {0x1EBD, 0xCF},
    {0x1EB8, 0xD1}, {0x1EB9, 0xD1},
    // KEY_D: Đ, đ
    {0x0110, 0xA7}, {0x0111, 0xAE},
    // KEY_A|TONE_MASK: Ấ, ấ, Ầ, ầ, Ẩ, ẩ, Ẫ, ẫ, Ậ, ậ
    {0x1EA4, 0xCA}, {0x1EA5, 0xCA}, {0x1EA6, 0xC7}, {0x1EA7, 0xC7},
    {0x1EA8, 0xC8}, {0x1EA9, 0xC8}, {0x1EAA, 0xC9}, {0x1EAB, 0xC9},
    {0x1EAC, 0xCB}, {0x1EAD, 0xCB},
    // KEY_A|TONEW_MASK: Ắ, ắ, Ằ, ằ, Ẳ, ẳ, Ẵ, ẵ, Ặ, ặ
    {0x1EAE, 0xBE}, {0x1EAF, 0xBE}, {0x1EB0, 0xBB}, {0x1EB1, 0xBB},
    {0x1EB2, 0xBC}, {0x1EB3, 0xBC}, {0x1EB4, 0xBD}, {0x1EB5, 0xBD},
    {0x1EB6, 0xC6}, {0x1EB7, 0xC6},
    // KEY_O|TONE_MASK: Ố, ố, Ồ, ồ, Ổ, ổ, Ỗ, ỗ, Ộ, ộ
    {0x1ED0, 0xE8}, {0x1ED1, 0xE8}, {0x1ED2, 0xE5}, {0x1ED3, 0xE5},
    {0x1ED4, 0xE6}, {0x1ED5, 0xE6}, {0x1ED6, 0xE7}, {0x1ED7, 0xE7},
    {0x1ED8, 0xE9}, {0x1ED9, 0xE9},
    // KEY_O|TONEW_MASK: Ớ, ớ, Ờ, ờ, Ở, ở, Ỡ, ỡ, Ợ, ợ
    {0x1EDA, 0xED}, {0x1EDB, 0xED}, {0x1EDC, 0xEA}, {0x1EDD, 0xEA},
    {0x1EDE, 0xEB}, {0x1EDF, 0xEB}, {0x1EE0, 0xEC}, {0x1EE1, 0xEC},
    {0x1EE2, 0xEE}, {0x1EE3, 0xEE},
    // KEY_U|TONEW_MASK: Ứ, ứ, Ừ, ừ, Ử, ử, Ữ, ữ, Ự, ự
    {0x1EE8, 0xF8}, {0x1EE9, 0xF8}, {0x1EEA, 0xF5}, {0x1EEB, 0xF5},
    {0x1EEC, 0xF6}, {0x1EED, 0xF6}, {0x1EEE, 0xF7}, {0x1EEF, 0xF7},
    {0x1EF0, 0xF9}, {0x1EF1, 0xF9},
    // KEY_E|TONE_MASK: Ế, ế, Ề, ề, Ể, ể, Ễ, ễ, Ệ, ệ
    {0x1EBE, 0xD5}, {0x1EBF, 0xD5}, {0x1EC0, 0xD2}, {0x1EC1, 0xD2},
    {0x1EC2, 0xD3}, {0x1EC3, 0xD3}, {0x1EC4, 0xD4}, {0x1EC5, 0xD4},
    {0x1EC6, 0xD6}, {0x1EC7, 0xD6},
    // KEY_I: Í, í, Ì, ì, Ỉ, ỉ, Ĩ, ĩ, Ị, ị
    {0x00CD, 0xDD}, {0x00ED, 0xDD}, {0x00CC, 0xD7}, {0x00EC, 0xD7},
    {0x1EC8, 0xD8}, {0x1EC9, 0xD8}, {0x0128, 0xDC}, {0x0129, 0xDC},
    {0x1ECA, 0xDE}, {0x1ECB, 0xDE},
    // KEY_Y: Ý, ý, Ỳ, ỳ, Ỷ, ỷ, Ỹ, ỹ, Ỵ, ỵ
    {0x00DD, 0xFD}, {0x00FD, 0xFD}, {0x1EF2, 0xFA}, {0x1EF3, 0xFA},
    {0x1EF6, 0xFB}, {0x1EF7, 0xFB}, {0x1EF8, 0xFC}, {0x1EF9, 0xFC},
    {0x1EF4, 0xFE}, {0x1EF5, 0xFE},
    };
    return kTcvn3Map;
}

// VNI Windows — HIBYTE:LOBYTE encoding, LOBYTE first
static const CodeMap& GetVniMap() {
    static const CodeMap kVniMap = {
    // KEY_A: Â, â, Ă, ă, Á, á, À, à, Ả, ả, Ã, ã, Ạ, ạ
    {0x00C2, 0xC241}, {0x00E2, 0xE261}, {0x0102, 0xCA41}, {0x0103, 0xEA61},
    {0x00C1, 0xD941}, {0x00E1, 0xF961}, {0x00C0, 0xD841}, {0x00E0, 0xF861},
    {0x1EA2, 0xDB41}, {0x1EA3, 0xFB61}, {0x00C3, 0xD541}, {0x00E3, 0xF561},
    {0x1EA0, 0xCF41}, {0x1EA1, 0xEF61},
    // KEY_O: Ô, ô, Ơ, ơ, Ó, ó, Ò, ò, Ỏ, ỏ, Õ, õ, Ọ, ọ
    {0x00D4, 0xC24F}, {0x00F4, 0xE26F}, {0x01A0, 0x00D4}, {0x01A1, 0x00F4},
    {0x00D3, 0xD94F}, {0x00F3, 0xF96F}, {0x00D2, 0xD84F}, {0x00F2, 0xF86F},
    {0x1ECE, 0xDB4F}, {0x1ECF, 0xFB6F}, {0x00D5, 0xD54F}, {0x00F5, 0xF56F},
    {0x1ECC, 0xCF4F}, {0x1ECD, 0xEF6F},
    // KEY_U: Ư, ư, Ú, ú, Ù, ù, Ủ, ủ, Ũ, ũ, Ụ, ụ
    {0x01AF, 0x00D6}, {0x01B0, 0x00F6},
    {0x00DA, 0xD955}, {0x00FA, 0xF975}, {0x00D9, 0xD855}, {0x00F9, 0xF875},
    {0x1EE6, 0xDB55}, {0x1EE7, 0xFB75}, {0x0168, 0xD555}, {0x0169, 0xF575},
    {0x1EE4, 0xCF55}, {0x1EE5, 0xEF75},
    // KEY_E: Ê, ê, É, é, È, è, Ẻ, ẻ, Ẽ, ẽ, Ẹ, ẹ
    {0x00CA, 0xC245}, {0x00EA, 0xE265},
    {0x00C9, 0xD945}, {0x00E9, 0xF965}, {0x00C8, 0xD845}, {0x00E8, 0xF865},
    {0x1EBA, 0xDB45}, {0x1EBB, 0xFB65}, {0x1EBC, 0xD545}, {0x1EBD, 0xF565},
    {0x1EB8, 0xCF45}, {0x1EB9, 0xEF65},
    // KEY_D: Đ, đ
    {0x0110, 0x00D1}, {0x0111, 0x00F1},
    // KEY_A|TONE_MASK: Ấ, ấ, Ầ, ầ, Ẩ, ẩ, Ẫ, ẫ, Ậ, ậ
    {0x1EA4, 0xC141}, {0x1EA5, 0xE161}, {0x1EA6, 0xC041}, {0x1EA7, 0xE061},
    {0x1EA8, 0xC541}, {0x1EA9, 0xE561}, {0x1EAA, 0xC341}, {0x1EAB, 0xE361},
    {0x1EAC, 0xC441}, {0x1EAD, 0xE461},
    // KEY_A|TONEW_MASK: Ắ, ắ, Ằ, ằ, Ẳ, ẳ, Ẵ, ẵ, Ặ, ặ
    {0x1EAE, 0xC941}, {0x1EAF, 0xE961}, {0x1EB0, 0xC841}, {0x1EB1, 0xE861},
    {0x1EB2, 0xDA41}, {0x1EB3, 0xFA61}, {0x1EB4, 0xDC41}, {0x1EB5, 0xFC61},
    {0x1EB6, 0xCB41}, {0x1EB7, 0xEB61},
    // KEY_O|TONE_MASK: Ố, ố, Ồ, ồ, Ổ, ổ, Ỗ, ỗ, Ộ, ộ
    {0x1ED0, 0xC14F}, {0x1ED1, 0xE16F}, {0x1ED2, 0xC04F}, {0x1ED3, 0xE06F},
    {0x1ED4, 0xC54F}, {0x1ED5, 0xE56F}, {0x1ED6, 0xC34F}, {0x1ED7, 0xE36F},
    {0x1ED8, 0xC44F}, {0x1ED9, 0xE46F},
    // KEY_O|TONEW_MASK: Ớ, ớ, Ờ, ờ, Ở, ở, Ỡ, ỡ, Ợ, ợ
    {0x1EDA, 0xD9D4}, {0x1EDB, 0xF9F4}, {0x1EDC, 0xD8D4}, {0x1EDD, 0xF8F4},
    {0x1EDE, 0xDBD4}, {0x1EDF, 0xFBF4}, {0x1EE0, 0xD5D4}, {0x1EE1, 0xF5F4},
    {0x1EE2, 0xCFD4}, {0x1EE3, 0xEFF4},
    // KEY_U|TONEW_MASK: Ứ, ứ, Ừ, ừ, Ử, ử, Ữ, ữ, Ự, ự
    {0x1EE8, 0xD9D6}, {0x1EE9, 0xF9F6}, {0x1EEA, 0xD8D6}, {0x1EEB, 0xF8F6},
    {0x1EEC, 0xDBD6}, {0x1EED, 0xFBF6}, {0x1EEE, 0xD5D6}, {0x1EEF, 0xF5F6},
    {0x1EF0, 0xCFD6}, {0x1EF1, 0xEFF6},
    // KEY_E|TONE_MASK: Ế, ế, Ề, ề, Ể, ể, Ễ, ễ, Ệ, ệ
    {0x1EBE, 0xC145}, {0x1EBF, 0xE165}, {0x1EC0, 0xC045}, {0x1EC1, 0xE065},
    {0x1EC2, 0xC545}, {0x1EC3, 0xE565}, {0x1EC4, 0xC345}, {0x1EC5, 0xE365},
    {0x1EC6, 0xC445}, {0x1EC7, 0xE465},
    // KEY_I: Í, í, Ì, ì, Ỉ, ỉ, Ĩ, ĩ, Ị, ị
    {0x00CD, 0x00CD}, {0x00ED, 0x00ED}, {0x00CC, 0x00CC}, {0x00EC, 0x00EC},
    {0x1EC8, 0x00C6}, {0x1EC9, 0x00E6}, {0x0128, 0x00D3}, {0x0129, 0x00F3},
    {0x1ECA, 0x00D2}, {0x1ECB, 0x00F2},
    // KEY_Y: Ý, ý, Ỳ, ỳ, Ỷ, ỷ, Ỹ, ỹ, Ỵ, ỵ
    {0x00DD, 0xD959}, {0x00FD, 0xF979}, {0x1EF2, 0xD859}, {0x1EF3, 0xF879},
    {0x1EF6, 0xDB59}, {0x1EF7, 0xFB79}, {0x1EF8, 0xD559}, {0x1EF9, 0xF579},
    {0x1EF4, 0x00CE}, {0x1EF5, 0x00EE},
    };
    return kVniMap;
}

// Unicode Compound — base char + combining mark
// HIBYTE encodes the combining mark index, LOBYTE is the base character
// Combining marks: 0x20=sắc(0x0301), 0x40=huyền(0x0300), 0x60=hỏi(0x0309),
//                  0x80=ngã(0x0303), 0xA0=nặng(0x0323)
// Special: values <= 0xFF or HIBYTE < 0x20 → single char (precomposed, no tone)
static const CodeMap& GetUnicodeCompoundMap() {
    static const CodeMap kUnicodeCompoundMap = {
    // KEY_A: Â, â, Ă, ă, Á, á, À, à, Ả, ả, Ã, ã, Ạ, ạ
    {0x00C2, 0x00C2}, {0x00E2, 0x00E2}, {0x0102, 0x0102}, {0x0103, 0x0103},
    {0x00C1, 0x2041}, {0x00E1, 0x2061}, {0x00C0, 0x4041}, {0x00E0, 0x4061},
    {0x1EA2, 0x6041}, {0x1EA3, 0x6061}, {0x00C3, 0x8041}, {0x00E3, 0x8061},
    {0x1EA0, 0xA041}, {0x1EA1, 0xA061},
    // KEY_O: Ô, ô, Ơ, ơ, Ó, ó, Ò, ò, Ỏ, ỏ, Õ, õ, Ọ, ọ
    {0x00D4, 0x00D4}, {0x00F4, 0x00F4}, {0x01A0, 0x01A0}, {0x01A1, 0x01A1},
    {0x00D3, 0x204F}, {0x00F3, 0x206F}, {0x00D2, 0x404F}, {0x00F2, 0x406F},
    {0x1ECE, 0x604F}, {0x1ECF, 0x606F}, {0x00D5, 0x804F}, {0x00F5, 0x806F},
    {0x1ECC, 0xA04F}, {0x1ECD, 0xA06F},
    // KEY_U: Ư, ư, Ú, ú, Ù, ù, Ủ, ủ, Ũ, ũ, Ụ, ụ
    {0x01AF, 0x01AF}, {0x01B0, 0x01B0},
    {0x00DA, 0x2055}, {0x00FA, 0x2075}, {0x00D9, 0x4055}, {0x00F9, 0x4075},
    {0x1EE6, 0x6055}, {0x1EE7, 0x6075}, {0x0168, 0x8055}, {0x0169, 0x8075},
    {0x1EE4, 0xA055}, {0x1EE5, 0xA075},
    // KEY_E: Ê, ê, É, é, È, è, Ẻ, ẻ, Ẽ, ẽ, Ẹ, ẹ
    {0x00CA, 0x00CA}, {0x00EA, 0x00EA},
    {0x00C9, 0x2045}, {0x00E9, 0x2065}, {0x00C8, 0x4045}, {0x00E8, 0x4065},
    {0x1EBA, 0x6045}, {0x1EBB, 0x6065}, {0x1EBC, 0x8045}, {0x1EBD, 0x8065},
    {0x1EB8, 0xA045}, {0x1EB9, 0xA065},
    // KEY_D: Đ, đ
    {0x0110, 0x0110}, {0x0111, 0x0111},
    // KEY_A|TONE_MASK: Ấ, ấ, Ầ, ầ, Ẩ, ẩ, Ẫ, ẫ, Ậ, ậ
    {0x1EA4, 0x20C2}, {0x1EA5, 0x20E2}, {0x1EA6, 0x40C2}, {0x1EA7, 0x40E2},
    {0x1EA8, 0x60C2}, {0x1EA9, 0x60E2}, {0x1EAA, 0x80C2}, {0x1EAB, 0x80E2},
    {0x1EAC, 0xA0C2}, {0x1EAD, 0xA0E2},
    // KEY_A|TONEW_MASK: Ắ, ắ, Ằ, ằ, Ẳ, ẳ, Ẵ, ẵ, Ặ, ặ
    {0x1EAE, 0x2102}, {0x1EAF, 0x2103}, {0x1EB0, 0x4102}, {0x1EB1, 0x4103},
    {0x1EB2, 0x6102}, {0x1EB3, 0x6103}, {0x1EB4, 0x8102}, {0x1EB5, 0x8103},
    {0x1EB6, 0xA102}, {0x1EB7, 0xA103},
    // KEY_O|TONE_MASK: Ố, ố, Ồ, ồ, Ổ, ổ, Ỗ, ỗ, Ộ, ộ
    {0x1ED0, 0x20D4}, {0x1ED1, 0x20F4}, {0x1ED2, 0x40D4}, {0x1ED3, 0x40F4},
    {0x1ED4, 0x60D4}, {0x1ED5, 0x60F4}, {0x1ED6, 0x80D4}, {0x1ED7, 0x80F4},
    {0x1ED8, 0xA0D4}, {0x1ED9, 0xA0F4},
    // KEY_O|TONEW_MASK: Ớ, ớ, Ờ, ờ, Ở, ở, Ỡ, ỡ, Ợ, ợ
    {0x1EDA, 0x21A0}, {0x1EDB, 0x21A1}, {0x1EDC, 0x41A0}, {0x1EDD, 0x41A1},
    {0x1EDE, 0x61A0}, {0x1EDF, 0x61A1}, {0x1EE0, 0x81A0}, {0x1EE1, 0x81A1},
    {0x1EE2, 0xA1A0}, {0x1EE3, 0xA1A1},
    // KEY_U|TONEW_MASK: Ứ, ứ, Ừ, ừ, Ử, ử, Ữ, ữ, Ự, ự
    {0x1EE8, 0x21AF}, {0x1EE9, 0x21B0}, {0x1EEA, 0x41AF}, {0x1EEB, 0x41B0},
    {0x1EEC, 0x61AF}, {0x1EED, 0x61B0}, {0x1EEE, 0x81AF}, {0x1EEF, 0x81B0},
    {0x1EF0, 0xA1AF}, {0x1EF1, 0xA1B0},
    // KEY_E|TONE_MASK: Ế, ế, Ề, ề, Ể, ể, Ễ, ễ, Ệ, ệ
    {0x1EBE, 0x20CA}, {0x1EBF, 0x20EA}, {0x1EC0, 0x40CA}, {0x1EC1, 0x40EA},
    {0x1EC2, 0x60CA}, {0x1EC3, 0x60EA}, {0x1EC4, 0x80CA}, {0x1EC5, 0x80EA},
    {0x1EC6, 0xA0CA}, {0x1EC7, 0xA0EA},
    // KEY_I: Í, í, Ì, ì, Ỉ, ỉ, Ĩ, ĩ, Ị, ị
    {0x00CD, 0x2049}, {0x00ED, 0x2069}, {0x00CC, 0x4049}, {0x00EC, 0x4069},
    {0x1EC8, 0x6049}, {0x1EC9, 0x6069}, {0x0128, 0x8049}, {0x0129, 0x8069},
    {0x1ECA, 0xA049}, {0x1ECB, 0xA069},
    // KEY_Y: Ý, ý, Ỳ, ỳ, Ỷ, ỷ, Ỹ, ỹ, Ỵ, ỵ
    {0x00DD, 0x2059}, {0x00FD, 0x2079}, {0x1EF2, 0x4059}, {0x1EF3, 0x4079},
    {0x1EF6, 0x6059}, {0x1EF7, 0x6079}, {0x1EF8, 0x8059}, {0x1EF9, 0x8079},
    {0x1EF4, 0xA059}, {0x1EF5, 0xA079},
    };
    return kUnicodeCompoundMap;
}

// Vietnamese Locale (CP 1258) — similar 2-byte encoding as VNI
static const CodeMap& GetCp1258Map() {
    static const CodeMap kCp1258Map = {
    // KEY_A: Â, â, Ă, ă, Á, á, À, à, Ả, ả, Ã, ã, Ạ, ạ
    {0x00C2, 0x00C2}, {0x00E2, 0x00E2}, {0x0102, 0x00C3}, {0x0103, 0x00E3},
    {0x00C1, 0xEC41}, {0x00E1, 0xEC61}, {0x00C0, 0xCC41}, {0x00E0, 0xCC61},
    {0x1EA2, 0xD241}, {0x1EA3, 0xD261}, {0x00C3, 0xDE41}, {0x00E3, 0xDE61},
    {0x1EA0, 0xF241}, {0x1EA1, 0xF261},
    // KEY_O: Ô, ô, Ơ, ơ, Ó, ó, Ò, ò, Ỏ, ỏ, Õ, õ, Ọ, ọ
    {0x00D4, 0x00D4}, {0x00F4, 0x00F4}, {0x01A0, 0x00D5}, {0x01A1, 0x00F5},
    {0x00D3, 0xEC4F}, {0x00F3, 0xEC6F}, {0x00D2, 0xCC4F}, {0x00F2, 0xCC6F},
    {0x1ECE, 0xD24F}, {0x1ECF, 0xD26F}, {0x00D5, 0xDE4F}, {0x00F5, 0xDE6F},
    {0x1ECC, 0xF24F}, {0x1ECD, 0xF26F},
    // KEY_U: Ư, ư, Ú, ú, Ù, ù, Ủ, ủ, Ũ, ũ, Ụ, ụ
    {0x01AF, 0x00DD}, {0x01B0, 0x00FD},
    {0x00DA, 0xEC55}, {0x00FA, 0xEC75}, {0x00D9, 0xCC55}, {0x00F9, 0xCC75},
    {0x1EE6, 0xD255}, {0x1EE7, 0xD275}, {0x0168, 0xDE55}, {0x0169, 0xDE75},
    {0x1EE4, 0xF255}, {0x1EE5, 0xF275},
    // KEY_E: Ê, ê, É, é, È, è, Ẻ, ẻ, Ẽ, ẽ, Ẹ, ẹ
    {0x00CA, 0x00CA}, {0x00EA, 0x00EA},
    {0x00C9, 0xEC45}, {0x00E9, 0xEC65}, {0x00C8, 0xCC45}, {0x00E8, 0xCC65},
    {0x1EBA, 0xD245}, {0x1EBB, 0xD265}, {0x1EBC, 0xDE45}, {0x1EBD, 0xDE65},
    {0x1EB8, 0xF245}, {0x1EB9, 0xF265},
    // KEY_D: Đ, đ
    {0x0110, 0x00D0}, {0x0111, 0x00F0},
    // KEY_A|TONE_MASK: Ấ, ấ, Ầ, ầ, Ẩ, ẩ, Ẫ, ẫ, Ậ, ậ
    {0x1EA4, 0xECC2}, {0x1EA5, 0xECE2}, {0x1EA6, 0xCCC2}, {0x1EA7, 0xCCE2},
    {0x1EA8, 0xD2C2}, {0x1EA9, 0xD2E2}, {0x1EAA, 0xDEC2}, {0x1EAB, 0xDEE2},
    {0x1EAC, 0xF2C2}, {0x1EAD, 0xF2E2},
    // KEY_A|TONEW_MASK: Ắ, ắ, Ằ, ằ, Ẳ, ẳ, Ẵ, ẵ, Ặ, ặ
    {0x1EAE, 0xECC3}, {0x1EAF, 0xECE3}, {0x1EB0, 0xCCC3}, {0x1EB1, 0xCCE3},
    {0x1EB2, 0xD2C3}, {0x1EB3, 0xD2E3}, {0x1EB4, 0xDEC3}, {0x1EB5, 0xDEE3},
    {0x1EB6, 0xF2C3}, {0x1EB7, 0xF2E3},
    // KEY_O|TONE_MASK: Ố, ố, Ồ, ồ, Ổ, ổ, Ỗ, ỗ, Ộ, ộ
    {0x1ED0, 0xECD4}, {0x1ED1, 0xECF4}, {0x1ED2, 0xCCD4}, {0x1ED3, 0xCCF4},
    {0x1ED4, 0xD2D4}, {0x1ED5, 0xD2F4}, {0x1ED6, 0xDED4}, {0x1ED7, 0xDEF4},
    {0x1ED8, 0xF2D4}, {0x1ED9, 0xF2F4},
    // KEY_O|TONEW_MASK: Ớ, ớ, Ờ, ờ, Ở, ở, Ỡ, ỡ, Ợ, ợ
    {0x1EDA, 0xECD5}, {0x1EDB, 0xECF5}, {0x1EDC, 0xCCD5}, {0x1EDD, 0xCCF5},
    {0x1EDE, 0xD2D5}, {0x1EDF, 0xD2F5}, {0x1EE0, 0xDED5}, {0x1EE1, 0xDEF5},
    {0x1EE2, 0xF2D5}, {0x1EE3, 0xF2F5},
    // KEY_U|TONEW_MASK: Ứ, ứ, Ừ, ừ, Ử, ử, Ữ, ữ, Ự, ự
    {0x1EE8, 0xECDD}, {0x1EE9, 0xECFD}, {0x1EEA, 0xCCDD}, {0x1EEB, 0xCCFD},
    {0x1EEC, 0xD2DD}, {0x1EED, 0xD2FD}, {0x1EEE, 0xDEDD}, {0x1EEF, 0xDEFD},
    {0x1EF0, 0xF2DD}, {0x1EF1, 0xF2FD},
    // KEY_E|TONE_MASK: Ế, ế, Ề, ề, Ể, ể, Ễ, ễ, Ệ, ệ
    {0x1EBE, 0xECCA}, {0x1EBF, 0xECEA}, {0x1EC0, 0xCCCA}, {0x1EC1, 0xCCEA},
    {0x1EC2, 0xD2CA}, {0x1EC3, 0xD2EA}, {0x1EC4, 0xDECA}, {0x1EC5, 0xDEEA},
    {0x1EC6, 0xF2CA}, {0x1EC7, 0xF2EA},
    // KEY_I: Í, í, Ì, ì, Ỉ, ỉ, Ĩ, ĩ, Ị, ị
    {0x00CD, 0xEC49}, {0x00ED, 0xEC69}, {0x00CC, 0xCC49}, {0x00EC, 0xCC69},
    {0x1EC8, 0xD249}, {0x1EC9, 0xD269}, {0x0128, 0xDE49}, {0x0129, 0xDE69},
    {0x1ECA, 0xF249}, {0x1ECB, 0xF269},
    // KEY_Y: Ý, ý, Ỳ, ỳ, Ỷ, ỷ, Ỹ, ỹ, Ỵ, ỵ
    {0x00DD, 0xEC59}, {0x00FD, 0xEC79}, {0x1EF2, 0xCC59}, {0x1EF3, 0xCC79},
    {0x1EF6, 0xD259}, {0x1EF7, 0xD279}, {0x1EF8, 0xDE59}, {0x1EF9, 0xDE79},
    {0x1EF4, 0xF259}, {0x1EF5, 0xF279},
    };
    return kCp1258Map;
}

// Unicode Compound combining marks: sắc, huyền, hỏi, ngã, nặng
static constexpr wchar_t kCombiningMarks[] = {0x0301, 0x0300, 0x0309, 0x0303, 0x0323};

// ═══════════════════════════════════════════════════════════
// Decode helpers
// ═══════════════════════════════════════════════════════════

/// Decode a VNI/CP1258 encoded value into 1 or 2 output units.
/// If value <= 0xFF → single char. Otherwise LOBYTE first, HIBYTE second.
static EncodedChar DecodeLoHi(uint32_t value) noexcept {
    EncodedChar result;
    if (value <= 0xFF) {
        result.units[0] = static_cast<wchar_t>(value);
        result.count = 1;
    } else {
        result.units[0] = static_cast<wchar_t>(value & 0xFF);        // LOBYTE = base char
        result.units[1] = static_cast<wchar_t>((value >> 8) & 0xFF); // HIBYTE = tone/mark
        result.count = 2;
    }
    return result;
}

/// Decode a Unicode Compound encoded value.
/// HIBYTE encodes the combining mark: 0x20=sắc, 0x40=huyền, 0x60=hỏi, 0x80=ngã, 0xA0=nặng.
/// If value fits in a single precomposed char (HIBYTE < 0x20 or value <= 0x01FF), single output.
static EncodedChar DecodeCompound(uint32_t value) noexcept {
    EncodedChar result;
    uint8_t hi = static_cast<uint8_t>((value >> 8) & 0xFF);
    uint8_t lo = static_cast<uint8_t>(value & 0xFF);

    if (hi < 0x20) {
        // Single precomposed character (e.g., Â=0x00C2, Đ=0x0110)
        result.units[0] = static_cast<wchar_t>(value);
        result.count = 1;
    } else {
        // Base char (LOBYTE) + combining mark (from HIBYTE index)
        result.units[0] = static_cast<wchar_t>(lo);
        // Map HIBYTE to combining mark: 0x20→0, 0x40→1, 0x60→2, 0x80→3, 0xA0→4
        // But for Ă-based: HIBYTE starts at 0x21 (0x2102, 0x2103)
        // The index is (hi >> 5) - 1
        int markIndex = (hi >> 5) - 1;
        if (markIndex >= 0 && markIndex < 5) {
            result.units[1] = kCombiningMarks[markIndex];
            result.count = 2;
        } else {
            // Fallback: return as single char
            result.units[0] = static_cast<wchar_t>(value);
            result.count = 1;
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════

EncodedChar CodeTableConverter::ConvertChar(wchar_t ch, CodeTable table) noexcept {
    // Fast path: Unicode → pass through
    if (table == CodeTable::Unicode) {
        return EncodedChar{{ch, 0}, 1};
    }

    auto key = static_cast<uint32_t>(ch);

    switch (table) {
        case CodeTable::TCVN3: {
            const auto& m = GetTcvn3Map();
            auto it = m.find(key);
            if (it != m.end()) {
                return EncodedChar{{static_cast<wchar_t>(it->second), 0}, 1};
            }
            break;
        }
        case CodeTable::VNIWindows: {
            const auto& m = GetVniMap();
            auto it = m.find(key);
            if (it != m.end()) {
                return DecodeLoHi(it->second);
            }
            break;
        }
        case CodeTable::UnicodeCompound: {
            const auto& m = GetUnicodeCompoundMap();
            auto it = m.find(key);
            if (it != m.end()) {
                return DecodeCompound(it->second);
            }
            break;
        }
        case CodeTable::VietnameseLocale: {
            const auto& m = GetCp1258Map();
            auto it = m.find(key);
            if (it != m.end()) {
                return DecodeLoHi(it->second);
            }
            break;
        }
        default:
            break;
    }

    // Not in map → pass through as-is (ASCII, spaces, digits, consonants)
    return EncodedChar{{ch, 0}, 1};
}

// ═══════════════════════════════════════════════════════════
// Reverse maps for decoding (built once from forward maps)
// ═══════════════════════════════════════════════════════════

using ReverseMap = std::unordered_map<uint32_t, wchar_t>;

struct ReversePairMap {
    ReverseMap single;  // encoded single byte → unicode char
    ReverseMap pair;    // (first << 16 | second) → unicode char
};

// Returns true if ch is a lowercase Vietnamese character
static bool IsLowerViet(wchar_t ch) noexcept {
    return ToLowerVietnamese(ch) == ch && ToUpperVietnamese(ch) != ch;
}

// Build TCVN3 reverse: encoded_byte → Unicode (prefer lowercase for collisions)
static const ReverseMap& GetTcvn3Reverse() {
    static const auto map = [] {
        ReverseMap rev;
        for (const auto& [unicode, tcvn3] : GetTcvn3Map()) {
            auto wch = static_cast<wchar_t>(unicode);
            auto it = rev.find(tcvn3);
            if (it == rev.end()) {
                rev[tcvn3] = wch;
            } else {
                // Prefer lowercase for ambiguous TCVN3 bytes
                if (IsLowerViet(wch) && !IsLowerViet(it->second)) {
                    it->second = wch;
                }
            }
        }
        return rev;
    }();
    return map;
}

// Build VNI/CP1258 reverse pair maps from forward map
static ReversePairMap BuildReversePairMap(const CodeMap& forward) {
    ReversePairMap result;
    for (const auto& [unicode, encoded] : forward) {
        auto wch = static_cast<wchar_t>(unicode);
        if (encoded <= 0xFF) {
            // Single-byte encoding
            auto it = result.single.find(encoded);
            if (it == result.single.end()) {
                result.single[encoded] = wch;
            } else if (IsLowerViet(wch) && !IsLowerViet(it->second)) {
                it->second = wch;
            }
        } else {
            // Two-byte: LOBYTE first in stream, HIBYTE second
            uint32_t lo = encoded & 0xFF;
            uint32_t hi = (encoded >> 8) & 0xFF;
            uint32_t pairKey = (lo << 16) | hi;
            auto it = result.pair.find(pairKey);
            if (it == result.pair.end()) {
                result.pair[pairKey] = wch;
            } else if (IsLowerViet(wch) && !IsLowerViet(it->second)) {
                it->second = wch;
            }
        }
    }
    return result;
}

static const ReversePairMap& GetVniReverse() {
    static const auto map = BuildReversePairMap(GetVniMap());
    return map;
}

static const ReversePairMap& GetCp1258Reverse() {
    static const auto map = BuildReversePairMap(GetCp1258Map());
    return map;
}

// Build Unicode Compound reverse: (base << 16 | combining_mark) → Unicode
static const ReverseMap& GetCompoundReverse() {
    static const auto map = [] {
        ReverseMap rev;
        for (const auto& [unicode, encoded] : GetUnicodeCompoundMap()) {
            auto wch = static_cast<wchar_t>(unicode);
            auto decoded = DecodeCompound(encoded);
            if (decoded.count == 2) {
                uint32_t pairKey = (static_cast<uint32_t>(decoded.units[0]) << 16) |
                                    static_cast<uint32_t>(decoded.units[1]);
                auto it = rev.find(pairKey);
                if (it == rev.end()) {
                    rev[pairKey] = wch;
                } else if (IsLowerViet(wch) && !IsLowerViet(it->second)) {
                    it->second = wch;
                }
            }
            // Single-char compound entries are identity — no reverse needed
        }
        return rev;
    }();
    return map;
}

static bool IsCombiningMark(wchar_t ch) noexcept {
    return ch == 0x0300 || ch == 0x0301 || ch == 0x0303 || ch == 0x0309 || ch == 0x0323;
}

// Portable alpha check — std::iswalpha doesn't handle Vietnamese on Linux with C locale
static bool IsAlpha(wchar_t ch) noexcept {
    if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z')) return true;
    if (ch >= 0x00C0 && ch <= 0x024F) return true;  // Latin Extended (Vietnamese modifiers)
    if (ch >= 0x1EA0 && ch <= 0x1EF9) return true;  // Vietnamese toned vowels
    return false;
}

// Build diacritics removal map: Vietnamese char → ASCII base
static const std::unordered_map<wchar_t, wchar_t>& GetDiacriticsMap() {
    static const auto map = [] {
        std::unordered_map<wchar_t, wchar_t> m;

        // Base ASCII for each kTonedVowel row
        constexpr wchar_t rowBase[12] = {
            L'a', L'a', L'a',  // a, â, ă → a
            L'e', L'e',        // e, ê → e
            L'i',              // i → i
            L'o', L'o', L'o',  // o, ô, ơ → o
            L'u', L'u',        // u, ư → u
            L'y'               // y → y
        };

        // Map all toned vowels (and their uppercase) to base
        for (int row = 0; row < 12; ++row) {
            for (int tone = 0; tone < 5; ++tone) {
                wchar_t lower = kTonedVowel[row][tone];
                if (lower == 0) continue;
                wchar_t upper = ToUpperVietnamese(lower);
                m[lower] = rowBase[row];
                m[upper] = static_cast<wchar_t>(towupper(rowBase[row]));
            }
        }

        // Base modified vowels → base ASCII
        m[L'\x00E2'] = L'a'; m[L'\x00C2'] = L'A';  // â/Â → a/A
        m[L'\x0103'] = L'a'; m[L'\x0102'] = L'A';  // ă/Ă → a/A
        m[L'\x00EA'] = L'e'; m[L'\x00CA'] = L'E';  // ê/Ê → e/E
        m[L'\x00F4'] = L'o'; m[L'\x00D4'] = L'O';  // ô/Ô → o/O
        m[L'\x01A1'] = L'o'; m[L'\x01A0'] = L'O';  // ơ/Ơ → o/O
        m[L'\x01B0'] = L'u'; m[L'\x01AF'] = L'U';  // ư/Ư → u/U
        m[L'\x0111'] = L'd'; m[L'\x0110'] = L'D';  // đ/Đ → d/D

        return m;
    }();
    return map;
}

// ═══════════════════════════════════════════════════════════
// String-level public API
// ═══════════════════════════════════════════════════════════

std::wstring CodeTableConverter::DecodeString(const std::wstring& input, CodeTable from) noexcept {
    if (from == CodeTable::Unicode) return input;

    std::wstring output;
    output.reserve(input.size());
    const size_t len = input.size();

    if (from == CodeTable::TCVN3) {
        const auto& rev = GetTcvn3Reverse();
        for (size_t i = 0; i < len; ++i) {
            auto key = static_cast<uint32_t>(input[i]);
            auto it = rev.find(key);
            output += (it != rev.end()) ? it->second : input[i];
        }
    } else if (from == CodeTable::UnicodeCompound) {
        const auto& rev = GetCompoundReverse();
        for (size_t i = 0; i < len;) {
            if (i + 1 < len && IsCombiningMark(input[i + 1])) {
                uint32_t pairKey = (static_cast<uint32_t>(input[i]) << 16) |
                                    static_cast<uint32_t>(input[i + 1]);
                auto it = rev.find(pairKey);
                if (it != rev.end()) {
                    output += it->second;
                    i += 2;
                    continue;
                }
            }
            output += input[i];
            ++i;
        }
    } else {
        // VNI or CP1258
        const auto& maps = (from == CodeTable::VNIWindows) ? GetVniReverse() : GetCp1258Reverse();
        for (size_t i = 0; i < len;) {
            if (i + 1 < len) {
                uint32_t pairKey = (static_cast<uint32_t>(input[i]) << 16) |
                                    static_cast<uint32_t>(input[i + 1]);
                auto it = maps.pair.find(pairKey);
                if (it != maps.pair.end()) {
                    output += it->second;
                    i += 2;
                    continue;
                }
            }
            auto it = maps.single.find(static_cast<uint32_t>(input[i]));
            if (it != maps.single.end()) {
                output += it->second;
            } else {
                output += input[i];
            }
            ++i;
        }
    }

    return output;
}

std::wstring CodeTableConverter::EncodeString(const std::wstring& input, CodeTable to) noexcept {
    if (to == CodeTable::Unicode) return input;

    std::wstring output;
    output.reserve(input.size() * 2);
    for (wchar_t ch : input) {
        auto enc = ConvertChar(ch, to);
        output += enc.units[0];
        if (enc.count == 2) output += enc.units[1];
    }
    return output;
}

std::wstring CodeTableConverter::RemoveDiacritics(const std::wstring& input) noexcept {
    const auto& dmap = GetDiacriticsMap();
    std::wstring output;
    output.reserve(input.size());
    for (wchar_t ch : input) {
        auto it = dmap.find(ch);
        output += (it != dmap.end()) ? it->second : ch;
    }
    return output;
}

std::wstring CodeTableConverter::ToUpper(const std::wstring& input) noexcept {
    std::wstring output;
    output.reserve(input.size());
    for (wchar_t ch : input) {
        output += ToUpperVietnamese(ch);
    }
    return output;
}

std::wstring CodeTableConverter::ToLower(const std::wstring& input) noexcept {
    std::wstring output;
    output.reserve(input.size());
    for (wchar_t ch : input) {
        output += ToLowerVietnamese(ch);
    }
    return output;
}

std::wstring CodeTableConverter::ToSentenceCase(const std::wstring& input) noexcept {
    std::wstring output;
    output.reserve(input.size());
    bool atSentenceStart = true;
    for (wchar_t ch : input) {
        if (atSentenceStart && IsAlpha(ch)) {
            output += ToUpperVietnamese(ch);
            atSentenceStart = false;
        } else {
            output += IsAlpha(ch) ? ToLowerVietnamese(ch) : ch;
            if (ch == L'.' || ch == L'!' || ch == L'?' || ch == L'\n') {
                atSentenceStart = true;
            }
        }
    }
    return output;
}

std::wstring CodeTableConverter::ToTitleCase(const std::wstring& input) noexcept {
    std::wstring output;
    output.reserve(input.size());
    bool afterSpace = true;
    for (wchar_t ch : input) {
        if (afterSpace && IsAlpha(ch)) {
            output += ToUpperVietnamese(ch);
            afterSpace = false;
        } else {
            output += IsAlpha(ch) ? ToLowerVietnamese(ch) : ch;
            afterSpace = std::iswspace(ch) != 0;
        }
    }
    return output;
}

}  // namespace NextKey
