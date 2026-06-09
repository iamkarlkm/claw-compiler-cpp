// codegen/macho_writer.cpp - Mach-O 64-bit 对象文件写入器实现

#include "macho_writer.h"
#include <fstream>
#include <cstring>
#include <iostream>

namespace claw {
namespace codegen {

// ============================================================================
// Mach-O 常量
// ============================================================================

static constexpr uint32_t MH_MAGIC_64    = 0xFEEDFACF;
static constexpr uint32_t CPU_TYPE_X86_64 = 0x01000007;
static constexpr uint32_t CPU_SUBTYPE_X86_64_ALL = 0x80000003;
static constexpr uint32_t MH_OBJECT      = 0x1;
static constexpr uint32_t LC_SEGMENT_64  = 0x19;
static constexpr uint32_t LC_SYMTAB      = 0x2;
static constexpr uint32_t N_EXT          = 0x01;
static constexpr uint32_t N_SECT         = 0x0E;
static constexpr uint32_t N_UNDF         = 0x00;
static constexpr uint32_t S_ATTR_PURE_INSTRUCTIONS = 0x80000000;
static constexpr uint32_t MH_SUBSECTIONS_VIA_SYMBOLS = 0x2000;

// ============================================================================
// 辅助函数：写入小端整数到 vector
// ============================================================================

template<typename T>
static void push_le(std::vector<uint8_t>& out, T value) {
    for (size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

static void push_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
    out.insert(out.end(), data, data + len);
}

static void write_u32_at(std::vector<uint8_t>& out, size_t pos, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) {
        out[pos + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

static void write_u64_at(std::vector<uint8_t>& out, size_t pos, uint64_t value) {
    for (size_t i = 0; i < 8; ++i) {
        out[pos + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

static void pad_to(std::vector<uint8_t>& out, size_t align) {
    size_t rem = out.size() % align;
    if (rem != 0) {
        out.resize(out.size() + (align - rem), 0);
    }
}

// ============================================================================
// MachOWriter
// ============================================================================

void MachOWriter::add_section(const MachOSection& section) {
    sections_.push_back(section);
}

void MachOWriter::add_symbol(const MachOSymbol& symbol) {
    symbols_.push_back(symbol);
}

bool MachOWriter::write(const std::string& path) {
    std::vector<uint8_t> out;

    // 构建字符串表
    std::vector<std::string> strings;
    strings.push_back(""); // 第0项为空字符串
    for (const auto& sym : symbols_) {
        strings.push_back(sym.name);
    }

    std::vector<uint8_t> strtab_data;
    strtab_data.push_back(0);
    std::vector<uint32_t> str_offsets;
    str_offsets.push_back(0);
    for (size_t i = 1; i < strings.size(); ++i) {
        str_offsets.push_back(static_cast<uint32_t>(strtab_data.size()));
        strtab_data.insert(strtab_data.end(), strings[i].begin(), strings[i].end());
        strtab_data.push_back(0);
    }

    // 固定大小
    const size_t HEADER_SIZE = 32;
    const size_t SEGMENT_CMD_SIZE = 72;
    const size_t SECTION_SIZE = 80;
    const size_t SYMTAB_CMD_SIZE = 24;
    const size_t NLIST_SIZE = 16;
    const size_t RELOC_SIZE = 8;
    const size_t BUILD_VERSION_CMD_SIZE = 24;
    const uint32_t LC_BUILD_VERSION = 0x32;
    const uint32_t PLATFORM_MACOS = 1;

    uint32_t ncmds = 3; // LC_BUILD_VERSION + LC_SEGMENT_64 + LC_SYMTAB
    size_t seg_cmd_total = SEGMENT_CMD_SIZE + sections_.size() * SECTION_SIZE;
    size_t sizeofcmds = BUILD_VERSION_CMD_SIZE + seg_cmd_total + SYMTAB_CMD_SIZE;
    size_t data_start = HEADER_SIZE + sizeofcmds;

    // === 预留 Header ===
    size_t header_pos = out.size();
    out.resize(out.size() + HEADER_SIZE, 0);

    // === 写入 LC_BUILD_VERSION ===
    // Suppresses linker warning: "no platform load command found"
    push_le(out, LC_BUILD_VERSION);
    push_le(out, static_cast<uint32_t>(BUILD_VERSION_CMD_SIZE));
    push_le(out, PLATFORM_MACOS);
    push_le(out, uint32_t(0x000A0F00)); // minos: macOS 10.15.0
    push_le(out, uint32_t(0x000A0F00)); // sdk:  macOS 10.15.0
    push_le(out, uint32_t(0));          // ntools

    // === 预留 LC_SEGMENT_64 ===
    size_t segment_cmd_pos = out.size();
    out.resize(out.size() + seg_cmd_total, 0);

    // === 预留 LC_SYMTAB ===
    size_t symtab_cmd_pos = out.size();
    out.resize(out.size() + SYMTAB_CMD_SIZE, 0);

    // === 写入 Section Data ===
    std::vector<uint64_t> section_fileoffs;
    std::vector<uint64_t> section_addrs;
    std::vector<uint64_t> section_sizes;
    uint64_t current_addr = 0;

    for (size_t i = 0; i < sections_.size(); ++i) {
        size_t align = 1ULL << sections_[i].align;
        pad_to(out, align);

        section_fileoffs.push_back(out.size());
        section_addrs.push_back(current_addr);
        section_sizes.push_back(sections_[i].data.size());

        push_bytes(out, sections_[i].data.data(), sections_[i].data.size());

        size_t aligned_size = sections_[i].data.size();
        if (align > 1) {
            aligned_size = (aligned_size + align - 1) & ~(align - 1);
        }
        current_addr += aligned_size;
    }

    // === 写入 Relocation Tables ===
    std::vector<uint64_t> reloc_fileoffs;
    for (size_t i = 0; i < sections_.size(); ++i) {
        reloc_fileoffs.push_back(out.size());
        for (const auto& rel : sections_[i].relocations) {
            uint32_t sym_idx = 0;
            for (size_t s = 0; s < symbols_.size(); ++s) {
                if (symbols_[s].name == rel.symbol) {
                    sym_idx = static_cast<uint32_t>(s);
                    break;
                }
            }
            uint32_t r_info = (sym_idx & 0xFFFFFF) |
                              ((rel.pcrel ? 1U : 0U) << 24) |
                              ((rel.length & 3U) << 25) |
                              (1U << 27) |
                              ((rel.type & 0xFU) << 28);
            push_le(out, static_cast<int32_t>(rel.offset));
            push_le(out, r_info);
        }
    }

    // === 写入 Symbol Table ===
    pad_to(out, 8);
    size_t symtab_off = out.size();
    for (size_t i = 0; i < symbols_.size(); ++i) {
        push_le(out, str_offsets[i + 1]);
        uint8_t n_type;
        if (symbols_[i].undefined) {
            n_type = N_UNDF | N_EXT;
        } else {
            n_type = N_SECT;
            if (symbols_[i].global) n_type |= N_EXT;
        }
        out.push_back(n_type);
        out.push_back(static_cast<uint8_t>(symbols_[i].section));
        push_le(out, uint16_t(0));
        push_le(out, symbols_[i].value);
    }

    // === 写入 String Table ===
    size_t strtab_off = out.size();
    push_bytes(out, strtab_data.data(), strtab_data.size());

    // === Patch Header ===
    write_u32_at(out, header_pos + 0,  MH_MAGIC_64);
    write_u32_at(out, header_pos + 4,  CPU_TYPE_X86_64);
    write_u32_at(out, header_pos + 8,  CPU_SUBTYPE_X86_64_ALL);
    write_u32_at(out, header_pos + 12, MH_OBJECT);
    write_u32_at(out, header_pos + 16, ncmds);
    write_u32_at(out, header_pos + 20, static_cast<uint32_t>(sizeofcmds));
    write_u32_at(out, header_pos + 24, MH_SUBSECTIONS_VIA_SYMBOLS);
    write_u32_at(out, header_pos + 28, 0);

    // === Patch LC_SEGMENT_64 ===
    uint64_t total_vmsize = current_addr;
    uint64_t total_filesize = 0;
    if (!sections_.empty()) {
        size_t last = sections_.size() - 1;
        size_t align = 1ULL << sections_[last].align;
        size_t aligned_last = sections_[last].data.size();
        if (align > 1) {
            aligned_last = (aligned_last + align - 1) & ~(align - 1);
        }
        total_filesize = (section_fileoffs[last] + aligned_last) - data_start;
    }

    size_t sp = segment_cmd_pos;
    write_u32_at(out, sp + 0,  LC_SEGMENT_64);
    write_u32_at(out, sp + 4,  static_cast<uint32_t>(seg_cmd_total));
    sp += 24; // skip cmd (4) + cmdsize (4) + segname (16)
    write_u64_at(out, sp,      0);             // vmaddr
    write_u64_at(out, sp + 8,  total_vmsize);
    write_u64_at(out, sp + 16, data_start);
    write_u64_at(out, sp + 24, total_filesize);
    write_u32_at(out, sp + 32, 7);             // maxprot
    write_u32_at(out, sp + 36, 7);             // initprot
    write_u32_at(out, sp + 40, static_cast<uint32_t>(sections_.size()));
    write_u32_at(out, sp + 44, 0);             // flags

    // === Patch section_64 entries ===
    size_t sec_p = segment_cmd_pos + SEGMENT_CMD_SIZE;
    for (size_t i = 0; i < sections_.size(); ++i) {
        char sectname[16] = {};
        std::strncpy(sectname, sections_[i].name.c_str(), 15);
        for (int j = 0; j < 16; ++j) out[sec_p + j] = sectname[j];
        sec_p += 16;

        char segname[16] = {};
        std::strncpy(segname, sections_[i].segname.c_str(), 15);
        for (int j = 0; j < 16; ++j) out[sec_p + j] = segname[j];
        sec_p += 16;

        write_u64_at(out, sec_p,      section_addrs[i]);
        write_u64_at(out, sec_p + 8,  section_sizes[i]);
        write_u32_at(out, sec_p + 16, static_cast<uint32_t>(section_fileoffs[i]));
        write_u32_at(out, sec_p + 20, sections_[i].align);
        write_u32_at(out, sec_p + 24, static_cast<uint32_t>(reloc_fileoffs[i]));
        write_u32_at(out, sec_p + 28, static_cast<uint32_t>(sections_[i].relocations.size()));
        write_u32_at(out, sec_p + 32, sections_[i].flags);
        write_u32_at(out, sec_p + 36, 0);
        write_u32_at(out, sec_p + 40, 0);
        write_u32_at(out, sec_p + 44, 0);
        sec_p += 48;
    }

    // === Patch LC_SYMTAB ===
    size_t tp = symtab_cmd_pos;
    write_u32_at(out, tp + 0,  LC_SYMTAB);
    write_u32_at(out, tp + 4,  static_cast<uint32_t>(SYMTAB_CMD_SIZE));
    write_u32_at(out, tp + 8,  static_cast<uint32_t>(symtab_off));
    write_u32_at(out, tp + 12, static_cast<uint32_t>(symbols_.size()));
    write_u32_at(out, tp + 16, static_cast<uint32_t>(strtab_off));
    write_u32_at(out, tp + 20, static_cast<uint32_t>(strtab_data.size()));

    // === 写入文件 ===
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open " << path << " for writing\n";
        return false;
    }
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return file.good();
}

} // namespace codegen
} // namespace claw
