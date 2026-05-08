// codegen/macho_writer.h - Mach-O 64-bit 对象文件写入器
// 仅支持 x86-64 + macOS

#ifndef CLAW_MACHO_WRITER_H
#define CLAW_MACHO_WRITER_H

#include <cstdint>
#include <string>
#include <vector>

namespace claw {
namespace codegen {

// ============================================================================
// Mach-O 重定位条目
// ============================================================================

struct MachORelocation {
    uint64_t offset;      // 在 section 中的偏移
    std::string symbol;   // 目标符号名
    bool pcrel;           // 是否 PC-relative
    uint8_t length;       // 2=4字节, 3=8字节
    uint8_t type;         // RELOC_X86_64_*, RELOC_X86_64_SIGNED 等
    int64_t addend;       // 加数
};

// ============================================================================
// Mach-O Section
// ============================================================================

struct MachOSection {
    std::string name;          // 如 "__text"
    std::string segname;       // 如 "__TEXT"
    std::vector<uint8_t> data;
    uint32_t align = 4;        // 对齐: 1 << align (4 = 16字节)
    uint32_t flags = 0;
    std::vector<MachORelocation> relocations;
};

// ============================================================================
// Mach-O Symbol
// ============================================================================

struct MachOSymbol {
    std::string name;
    uint64_t value = 0;        // 在 section 中的偏移
    uint32_t section = 0;      // 1-based section index
    bool global = false;       // true = N_EXT, false = N_SECT
    bool undefined = false;    // true = N_UNDF (external undefined symbol)
};

// ============================================================================
// Mach-O Writer
// ============================================================================

class MachOWriter {
public:
    MachOWriter() = default;

    // 添加 section
    void add_section(const MachOSection& section);

    // 添加符号
    void add_symbol(const MachOSymbol& symbol);

    // 获取当前 section 数量
    size_t section_count() const { return sections_.size(); }

    // 写入 Mach-O 64-bit 对象文件
    bool write(const std::string& path);

private:
    std::vector<MachOSection> sections_;
    std::vector<MachOSymbol> symbols_;

    // 写入辅助
    void write_header(std::vector<uint8_t>& out, uint32_t ncmds, uint32_t sizeofcmds);
    void write_segment_command(std::vector<uint8_t>& out, const MachOSection& sect, uint64_t fileoff);
    void write_section_64(std::vector<uint8_t>& out, const MachOSection& sect, uint64_t addr, uint64_t fileoff);
    void write_symtab_command(std::vector<uint8_t>& out, uint32_t nsyms, uint32_t symoff, uint32_t stroff, uint32_t strsize);
    void write_symbol_table(std::vector<uint8_t>& out, const std::vector<std::string>& strings, uint32_t& stroff);
    void write_string_table(std::vector<uint8_t>& out, const std::vector<std::string>& strings);
    void write_relocations(std::vector<uint8_t>& out, const std::vector<MachORelocation>& relocs, const std::vector<std::string>& strings);
};

} // namespace codegen
} // namespace claw

#endif // CLAW_MACHO_WRITER_H
