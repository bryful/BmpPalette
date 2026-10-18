#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cwctype>
#include <limits>
#include <sstream>
#include <map>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs = std::filesystem;
struct Color { unsigned char r, g, b; };
struct Bitmap {
    uint64_t size{};
    uint32_t declared{}, offset{}, header{}, compression{}, imageSize{}, used{};
    int64_t width{}, height{};
    unsigned bits{};
    std::vector<Color> palette;
    bool png=false;
    unsigned sourceBits{};
    std::vector<unsigned char> pixels; // PNG decoded as top-down BGR24.
};
uint32_t u32(const std::vector<unsigned char>& b, size_t p) {
    if (p + 4 > b.size()) throw std::runtime_error("Truncated header.");
    return uint32_t(b[p]) | (uint32_t(b[p+1])<<8) | (uint32_t(b[p+2])<<16) | (uint32_t(b[p+3])<<24);
}
unsigned u16(const std::vector<unsigned char>& b, size_t p) {
    if (p + 2 > b.size()) throw std::runtime_error("Truncated header.");
    return unsigned(b[p]) | (unsigned(b[p+1])<<8);
}
int64_t signed32(uint32_t v) { return v <= 0x7fffffffU ? int64_t(v) : int64_t(v)-0x100000000LL; }
Bitmap readBitmap(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open input BMP.");
    auto length = in.tellg();
    if (length < 26) throw std::runtime_error("File too short for BMP.");
    Bitmap m; m.size = static_cast<uint64_t>(length);
    std::vector<unsigned char> b(26);
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(b.data()), b.size())) throw std::runtime_error("Cannot read header.");
    if (b[0]!='B' || b[1]!='M') throw std::runtime_error("Not a BMP file.");
    m.declared=u32(b,2); m.offset=u32(b,10); m.header=u32(b,14);
    if (m.header!=12 && m.header!=40 && m.header!=52 && m.header!=56 && m.header!=108 && m.header!=124)
        throw std::runtime_error("Unsupported DIB header.");
    size_t end=14+m.header;
    if (end>m.size || m.offset<end || m.offset>m.size) throw std::runtime_error("Invalid header or pixel offset.");
    b.resize(end); in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(b.data()), b.size())) throw std::runtime_error("Truncated header.");
    bool core=m.header==12;
    m.width=core ? u16(b,18) : signed32(u32(b,18));
    m.height=core ? u16(b,20) : signed32(u32(b,22));
    m.bits=u16(b,core?24:28);
    if (u16(b,core?22:26)!=1 || m.width<=0 || m.height==0 ||
        (m.bits!=1 && m.bits!=4 && m.bits!=8 && m.bits!=16 && m.bits!=24 && m.bits!=32))
        throw std::runtime_error("Invalid dimensions, planes or bit depth.");
    if (!core) { m.compression=u32(b,30); m.imageSize=u32(b,34); m.used=u32(b,46); }
    uint64_t start=end;
    if (m.header==40 && (m.compression==3 || m.compression==6)) start+=m.compression==3?12:16;
    unsigned maximum=m.bits<=8 ? 1U<<m.bits : 256;
    unsigned count=m.used ? m.used : (m.bits<=8 ? maximum : 0);
    if (count>maximum) throw std::runtime_error("Invalid palette color count.");
    unsigned entrySize=core?3:4;
    if (start+uint64_t(count)*entrySize>m.offset) throw std::runtime_error("Truncated palette or channel masks.");
    in.seekg(static_cast<std::streamoff>(start));
    for (unsigned i=0;i<count;++i) {
        std::array<unsigned char,4> c{};
        if (!in.read(reinterpret_cast<char*>(c.data()),entrySize)) throw std::runtime_error("Truncated palette.");
        m.palette.push_back({c[2],c[1],c[0]});
    }
    return m;
}

uint32_t rgb(Color c) { return (uint32_t(c.r)<<16)|(uint32_t(c.g)<<8)|c.b; }
Color color(uint32_t v) { return {static_cast<unsigned char>(v>>16),static_cast<unsigned char>(v>>8),static_cast<unsigned char>(v)}; }
std::wstring lower(std::wstring s) { for (auto& c:s) c=static_cast<wchar_t>(std::towlower(c)); return s; }
fs::path bmpPath(fs::path p) { if (!p.has_extension()) p+=L".bmp"; return p; }
void textPalette(std::ostream& out,const std::vector<Color>& palette) {
    for (auto c:palette) out << '#' << std::hex << std::setfill('0') << std::setw(6) << rgb(c) << '\n';
    out << std::dec << std::setfill(' ');
}
// Keep the old file intact until the complete replacement has reached disk.
void replacePaletteFile(const fs::path& output,const std::string& data) {
    struct Temporary {
        fs::path path;
        HANDLE file=INVALID_HANDLE_VALUE;
        ~Temporary() {
            if(file!=INVALID_HANDLE_VALUE) CloseHandle(file);
            if(!path.empty()) DeleteFileW(path.c_str());
        }
    } temporary;
    static std::atomic<uint64_t> sequence{0};
    for(unsigned attempt=0;attempt<100;++attempt) {
        auto candidate=output.parent_path()/(L".palette-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence++)+L".tmp");
        HANDLE file=CreateFileW(candidate.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE) {temporary.path=std::move(candidate);temporary.file=file;break;}
        if(GetLastError()!=ERROR_FILE_EXISTS && GetLastError()!=ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Cannot create temporary palette output.");
    }
    if(temporary.file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create unique temporary palette output.");
    for(size_t offset=0;offset<data.size();) {
        DWORD count=static_cast<DWORD>(std::min<size_t>(data.size()-offset,MAXDWORD)),written=0;
        if(!WriteFile(temporary.file,data.data()+offset,count,&written,nullptr) || written==0)
            throw std::runtime_error("Palette write failed.");
        offset+=written;
    }
    if(!FlushFileBuffers(temporary.file)) throw std::runtime_error("Palette flush failed.");
    HANDLE file=temporary.file;temporary.file=INVALID_HANDLE_VALUE;
    if(!CloseHandle(file)) throw std::runtime_error("Palette close failed.");
    if(!MoveFileExW(temporary.path.c_str(),output.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace palette output; existing file was preserved.");
    temporary.path.clear();
}
void savePalette(const fs::path& input,fs::path output,const std::vector<Color>& palette,bool generated=false) {
    if (!output.has_extension()) output+=L".gpl";
    auto ext=lower(output.extension().wstring());
    if (ext!=L".gpl" && ext!=L".json" && (generated || (ext!=L".txt" && ext!=L".act" && ext!=L".c")))
        throw std::runtime_error("Unsupported palette output extension.");
    if (!input.empty() && fs::exists(input) && fs::exists(output) && fs::equivalent(input,output)) throw std::runtime_error("Output must differ from input.");
    std::ostringstream out;
    if (ext==L".txt") textPalette(out,palette);
    else if (ext==L".json") {
        out << '[';
        for (size_t i=0;i<palette.size();++i) {
            if (i) out << ',';
            auto c=palette[i];
            out << '[' << unsigned(c.r) << ',' << unsigned(c.g) << ',' << unsigned(c.b) << ']';
        }
        out << "]\n";
    } else if (ext==L".c") {
        out << '{';
        for (size_t i=0;i<palette.size();++i) {
            if (i) out << ',';
            out << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6) << rgb(palette[i]);
        }
        out << "}\n";
    } else if (ext==L".act") {
        if (palette.size()>256) throw std::runtime_error("ACT supports at most 256 colors.");
        std::array<unsigned char,772> data{};
        for (size_t i=0;i<palette.size();++i) { data[i*3]=palette[i].r; data[i*3+1]=palette[i].g; data[i*3+2]=palette[i].b; }
        data[768]=static_cast<unsigned char>(palette.size()>>8); data[769]=static_cast<unsigned char>(palette.size()&255);
        data[770]=data[771]=255;
        out.write(reinterpret_cast<char*>(data.data()),data.size());
    } else {
        out << "GIMP Palette\nName: BMP palette\nColumns: 16\n#\n";
        for (size_t i=0;i<palette.size();++i) { auto c=palette[i]; out << unsigned(c.r) << ' ' << unsigned(c.g) << ' ' << unsigned(c.b) << "\tColor " << i << '\n'; }
    }
    if (!out) throw std::runtime_error("Palette serialization failed.");
    replacePaletteFile(output,out.str());
}
std::vector<Color> loadPalette(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open palette.");
    std::string text((std::istreambuf_iterator<char>(in)),{});
    if (text.starts_with("\xef\xbb\xbf")) text.erase(0,3);
    std::vector<Color> result;
    auto ext=lower(path.extension().wstring());
    if (ext==L".json") {
        std::istringstream s(text); char ch;
        if (!(s>>ch) || ch!='[') throw std::runtime_error("Expected JSON color array.");
        s>>std::ws;
        if (s.peek()==']') { s.get(); }
        else while (true) {
            s>>std::ws;
            if (s.peek()=='[') {
                s.get();
                unsigned channels[3]{};
                for (unsigned i=0;i<3;++i) {
                    s>>std::ws;
                    if (s.peek()<'0' || s.peek()>'9') throw std::runtime_error("RGB channels must be integers from 0 to 255.");
                    if (s.peek()=='0') {s.get();channels[i]=0;}
                    else if (!(s>>channels[i])) throw std::runtime_error("Invalid RGB channel.");
                    if (channels[i]>255 || !(s>>ch) || ch!=(i==2?']':',')) throw std::runtime_error("Expected RGB triplet [r,g,b], values 0 to 255.");
                }
                result.push_back({static_cast<unsigned char>(channels[0]),static_cast<unsigned char>(channels[1]),static_cast<unsigned char>(channels[2])});
            } else {
            s>>std::ws; std::string token;
            while (s && s.peek()!=',' && s.peek()!=']' && s.peek()!=EOF) token+=static_cast<char>(s.get());
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.pop_back();
            if (token.empty() || token[0]=='-' || token[0]=='+') throw std::runtime_error("Invalid palette color.");
            size_t consumed=0;
            int base=token.size()>2 && token[0]=='0' && (token[1]=='x'||token[1]=='X')?16:10;
            auto value=std::stoull(token,&consumed,base);
            if (consumed!=token.size() || value>0xffffff) throw std::runtime_error("Invalid palette RGB value.");
            result.push_back(color(static_cast<uint32_t>(value)));
            }
            if (result.size()>16) throw std::runtime_error("Reduction palette must contain exactly 16 colors.");
            if (!(s>>ch)) throw std::runtime_error("Unclosed palette array.");
            if (ch==']') break;
            if (ch!=',') throw std::runtime_error("Expected comma.");
        }
        s>>std::ws; if (!s.eof()) throw std::runtime_error("Unexpected text after palette array.");
    } else if (ext==L".gpl") {
        std::istringstream s(text); std::string line;
        if (!std::getline(s,line) || (line!="GIMP Palette" && line!="GIMP Palette\r")) throw std::runtime_error("Invalid GPL header.");
        while (std::getline(s,line)) {
            auto begin=line.find_first_not_of(" \t\r");
            if (begin==std::string::npos || line[begin]=='#' || line.compare(begin,5,"Name:")==0 || line.compare(begin,8,"Columns:")==0) continue;
            std::istringstream row(line); int r,g,b;
            if (!(row>>r>>g>>b) || r<0 || r>255 || g<0 || g>255 || b<0 || b>255) throw std::runtime_error("Invalid GPL color.");
            result.push_back({static_cast<unsigned char>(r),static_cast<unsigned char>(g),static_cast<unsigned char>(b)});
        }
    } else throw std::runtime_error("Input palette must be .gpl or .json.");
    if (result.size()!=16) throw std::runtime_error("Reduction palette must contain exactly 16 colors (index 0 reserved).");
    return result;
}
