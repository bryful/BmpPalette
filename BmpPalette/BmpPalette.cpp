#include "../Shared/ImageIO.h"
// Scan physical rows without loading a whole image. Row order is preserved on output.
void scanPixels(const fs::path& path,const Bitmap& m,const auto& visit) {
    if(m.png) {
        for(size_t y=0;y<static_cast<size_t>(-m.height);++y)
            for(size_t x=0;x<static_cast<size_t>(m.width);++x) {
                size_t p=(y*static_cast<size_t>(m.width)+x)*3;
                visit(x,y,Color{m.pixels[p+2],m.pixels[p+1],m.pixels[p]});
            }
        return;
    }
    if (m.compression!=0) throw std::runtime_error("Pixel processing requires uncompressed BI_RGB BMP.");
    if (m.bits!=1 && m.bits!=4 && m.bits!=8 && m.bits!=24 && m.bits!=32) throw std::runtime_error("Unsupported pixel format for reduction.");
    uint64_t stride=((uint64_t(m.width)*m.bits+31)/32)*4;
    uint64_t height=static_cast<uint64_t>(m.height<0?-m.height:m.height);
    if (stride>std::numeric_limits<size_t>::max() || height>(m.size-m.offset)/stride) throw std::runtime_error("Truncated pixel data.");
    std::ifstream in(path,std::ios::binary); in.seekg(m.offset);
    std::vector<unsigned char> row(static_cast<size_t>(stride));
    for (uint64_t y=0;y<height;++y) {
        if (!in.read(reinterpret_cast<char*>(row.data()),static_cast<std::streamsize>(row.size()))) throw std::runtime_error("Pixel read failed.");
        for (size_t x=0;x<static_cast<size_t>(m.width);++x) {
            Color c{};
            if (m.bits>=24) { size_t p=x*(m.bits/8); c={row[p+2],row[p+1],row[p]}; }
            else {
                unsigned index=m.bits==8?row[x]:m.bits==4?((row[x/2]>>(x%2?0:4))&15):((row[x/8]>>(7-x%8))&1);
                if (index>=m.palette.size()) throw std::runtime_error("Pixel references missing palette entry.");
                c=m.palette[index];
            }
            visit(x,y,c);
        }
    }
}
double distance(Color a,Color b) {
    double r=int(a.r)-int(b.r),g=int(a.g)-int(b.g),bl=int(a.b)-int(b.b);
    return r*r+g*g+bl*bl;
}
struct Sample { Color c; uint64_t weight; };
std::vector<Color> makePalette(const fs::path& input,const Bitmap& m) {
    if (m.bits!=24 || m.compression!=0) throw std::runtime_error("-makepal requires uncompressed RGB888 (24-bit) BMP or PNG.");
    struct Bin { uint64_t n{},r{},g{},b{}; };
    std::vector<Bin> bins(32768);
    std::map<uint32_t,uint64_t> exact;
    bool useExact=true;
    scanPixels(input,m,[&](size_t,uint64_t,Color c) {
        if (useExact) {
            ++exact[rgb(c)];
            if (exact.size()>32768) {exact.clear();useExact=false;}
        }
        auto& bin=bins[(unsigned(c.r>>3)<<10)|(unsigned(c.g>>3)<<5)|(c.b>>3)];
        ++bin.n; bin.r+=c.r; bin.g+=c.g; bin.b+=c.b;
    });
    std::vector<Sample> samples;
    if (useExact) {
        for (auto [value,weight]:exact) samples.push_back({color(value),weight});
    } else {
        for (auto b:bins) if (b.n) samples.push_back({{static_cast<unsigned char>((b.r+b.n/2)/b.n),static_cast<unsigned char>((b.g+b.n/2)/b.n),static_cast<unsigned char>((b.b+b.n/2)/b.n)},b.n});
    }
    // Deterministic weighted farthest-point initialization, followed by Lloyd refinement.
    auto most=std::max_element(samples.begin(),samples.end(),[](const auto& a,const auto& b){return a.weight<b.weight;});
    std::vector<Color> centers{most->c};
    while (centers.size()<15 && centers.size()<samples.size()) {
        double best=-1; Color chosen{};
        for (auto s:samples) {
            double d=std::numeric_limits<double>::max();
            for (auto c:centers) d=std::min(d,distance(s.c,c));
            double score=d*static_cast<double>(s.weight);
            if (score>best) { best=score; chosen=s.c; }
        }
        if (best<=0) break;
        centers.push_back(chosen);
    }
    for (int iteration=0;iteration<24;++iteration) {
        std::vector<Bin> sums(centers.size());
        for (auto s:samples) {
            size_t index=0; double best=distance(s.c,centers[0]);
            for (size_t i=1;i<centers.size();++i) { double d=distance(s.c,centers[i]); if (d<best) {best=d;index=i;} }
            auto& sum=sums[index]; sum.n+=s.weight; sum.r+=uint64_t(s.c.r)*s.weight; sum.g+=uint64_t(s.c.g)*s.weight; sum.b+=uint64_t(s.c.b)*s.weight;
        }
        bool changed=false;
        for (size_t i=0;i<centers.size();++i) if (sums[i].n) {
            auto s=sums[i]; Color c{static_cast<unsigned char>((s.r+s.n/2)/s.n),static_cast<unsigned char>((s.g+s.n/2)/s.n),static_cast<unsigned char>((s.b+s.n/2)/s.n)};
            changed|=rgb(c)!=rgb(centers[i]); centers[i]=c;
        }
        if (!changed) break;
    }
    while (centers.size()<15) centers.push_back(centers.back());
    centers.insert(centers.begin(),{255,0,255});
    return centers;
}
bool wildcard(const std::wstring& pattern,const std::wstring& name) {
    size_t p=0,n=0,star=std::wstring::npos,mark=0;
    auto pat=lower(pattern),value=lower(name);
    while (n<value.size()) {
        if (p<pat.size() && (pat[p]=='?' || pat[p]==value[n])) {++p;++n;}
        else if (p<pat.size() && pat[p]=='*') {star=p++;mark=n;}
        else if (star!=std::wstring::npos) {p=star+1;n=++mark;}
        else return false;
    }
    while (p<pat.size() && pat[p]=='*') ++p;
    return p==pat.size();
}
std::vector<fs::path> inputs(fs::path pattern) {
    pattern=bmpPath(pattern);
    if (pattern.filename().wstring().find_first_of(L"*?")==std::wstring::npos) return {pattern};
    std::vector<fs::path> result;
    fs::path dir=pattern.parent_path().empty()?fs::path(L"."):pattern.parent_path();
    for (const auto& entry:fs::directory_iterator(dir)) if (entry.is_regular_file() && wildcard(pattern.filename().wstring(),entry.path().filename().wstring())) result.push_back(entry.path());
    std::sort(result.begin(),result.end());
    if (result.empty()) throw std::runtime_error("No input images matched.");
    return result;
}
void put16(std::ostream& out,uint16_t v) { out.put(static_cast<char>(v));out.put(static_cast<char>(v>>8)); }
void put32(std::ostream& out,uint32_t v) { put16(out,static_cast<uint16_t>(v));put16(out,static_cast<uint16_t>(v>>16)); }
fs::path uniqueOutput(const fs::path& dir,const fs::path& input) {
    fs::path name=input.filename();name.replace_extension(L".bmp");
    fs::path candidate=dir/name;
    for (uint64_t i=1;fs::exists(candidate);++i) candidate=dir/(input.stem().wstring()+L"_"+std::to_wstring(i)+L".bmp");
    return candidate;
}
void reduce(const fs::path& input,const fs::path& dir,const std::vector<Color>& palette) {
    auto m=readImage(input);
    // Validate all input pixels before creating output.
    scanPixels(input,m,[](size_t,uint64_t,Color){});
    uint64_t stride=((uint64_t(m.width)*4+31)/32)*4;
    uint64_t height=static_cast<uint64_t>(m.height<0?-m.height:m.height);
    if (height>(UINT32_MAX-118ULL)/stride) throw std::runtime_error("Reduced BMP exceeds format size limit.");
    uint32_t bytes=static_cast<uint32_t>(stride*height);
    auto output=uniqueOutput(dir,input);
    std::ofstream out(output,std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open reduced BMP output.");
    out.put('B');out.put('M');put32(out,118+bytes);put32(out,0);put32(out,118);
    put32(out,40);put32(out,static_cast<uint32_t>(m.width));put32(out,static_cast<uint32_t>(m.height));
    put16(out,1);put16(out,4);put32(out,0);put32(out,bytes);put32(out,0);put32(out,0);put32(out,16);put32(out,0);
    for (auto c:palette) {out.put(static_cast<char>(c.b));out.put(static_cast<char>(c.g));out.put(static_cast<char>(c.r));out.put(0);}
    std::vector<unsigned char> row(static_cast<size_t>(stride));
    std::map<uint32_t,unsigned> cache;
    scanPixels(input,m,[&](size_t x,uint64_t,Color c) {
        unsigned index;
        auto it=cache.find(rgb(c));
        if (it!=cache.end()) index=it->second;
        else {
            index=1; double best=distance(c,palette[1]);
            for (unsigned i=2;i<16;++i) {double d=distance(c,palette[i]);if (d<best) {best=d;index=i;}}
            if (cache.size()<65536) cache.emplace(rgb(c),index);
        }
        if (x%2==0) row[x/2]=static_cast<unsigned char>(index<<4);else row[x/2]|=static_cast<unsigned char>(index);
        if (x+1==static_cast<size_t>(m.width)) {out.write(reinterpret_cast<char*>(row.data()),static_cast<std::streamsize>(row.size()));std::fill(row.begin(),row.end(),0);}
    });
    out.close();if (!out) throw std::runtime_error("Reduced BMP write failed.");
    std::wcout << output.wstring() << L'\n';
}
std::string codeName(const fs::path& input) {
    std::string name;
    for (wchar_t c:input.stem().wstring()) {
        if ((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9')) name+=static_cast<char>(c);
        else name+='_';
    }
    if (name.empty() || name[0]=='_' || (name[0]>='0' && name[0]<='9')) name="image_"+name;
    // Avoid reserved identifiers containing consecutive underscores.
    while (name.find("__")!=std::string::npos) name.replace(name.find("__"),2,"_");
    while (!name.empty() && name.back()=='_') name.pop_back();
    return name;
}
void exportCode(const fs::path& input,const fs::path& output) {
    if (lower(input.extension().wstring())!=L".bmp") throw std::runtime_error("-code requires a 4-bit, 16-color BMP.");
    auto m=readBitmap(input);
    if (m.bits!=4 || m.palette.size()!=16 || m.compression!=0)
        throw std::runtime_error("-code requires an uncompressed 4-bit BMP with 16 palette entries.");
    uint64_t height=static_cast<uint64_t>(m.height<0?-m.height:m.height);
    if (height>INT32_MAX) throw std::runtime_error("Image height exceeds generated int range.");
    scanPixels(input,m,[](size_t,uint64_t,Color){}); // Reject truncated data before opening output.
    if (fs::exists(output) && fs::equivalent(input,output)) throw std::runtime_error("Output must differ from input.");
    std::ifstream in(input,std::ios::binary);
    std::ofstream out(output,std::ios::binary);
    if (!in || !out) throw std::runtime_error("Cannot open code input/output.");
    auto name=codeName(input);
    out << "#include <stdint.h>\n\nconst int " << name << "_width = " << m.width
        << ";\nconst int " << name << "_height = " << height << ";\n"
        << "static const uint32_t " << name << "_palette[16] = {\n";
    out << std::hex << std::uppercase << std::setfill('0');
    for (size_t i=0;i<16;++i) {
        if (i%4==0) out << "    ";
        out << "0x" << std::setw(6) << rgb(m.palette[i]) << ',' << (i%4==3?'\n':' ');
    }
    out << "};\n\n// Top-down, 4bpp: high nibble first, each row byte-aligned.\n"
        << "static uint8_t " << name << "_data[] = {\n";
    size_t rowBytes=static_cast<size_t>((m.width+1)/2);
    uint64_t stride=((uint64_t(m.width)*4+31)/32)*4;
    std::vector<unsigned char> row(rowBytes);
    size_t column=0;
    for (uint64_t y=0;y<height;++y) {
        uint64_t physical=m.height<0?y:height-1-y;
        in.seekg(static_cast<std::streamoff>(m.offset+physical*stride));
        if (!in.read(reinterpret_cast<char*>(row.data()),static_cast<std::streamsize>(row.size()))) throw std::runtime_error("Pixel read failed.");
        if (m.width%2) row.back()&=0xF0; // Unused low nibble is deterministic.
        for (auto byte:row) {
            if (column==0) out << "    ";
            out << "0x" << std::setw(2) << unsigned(byte) << ',';
            if (++column==16) {out << '\n';column=0;} else out << ' ';
        }
    }
    if (column) out << '\n';
    out << "};\n";
    out.close();if (!out) throw std::runtime_error("Code write failed.");
    std::wcout << output.wstring() << L'\n';
}
void help() {
    std::cout << "BmpPalette\n"
        "  -info <bmp/png>              Show image information\n"
        "  -pal <bmp/png> [-o palette]   Print/export existing indexed palette\n"
        "  -makepal <bmp/png> [-o palette] Choose 15 colors + reserved index 0\n"
        "  -redu <bmp/png-pattern> -p palette [-o directory]\n"
        "                               Reduce to 4-bit BMP using indices 1..15\n"
        "  -code <bmp-pattern> [-o file/directory]\n"
        "                               Export 16-color BMP as packed C/C++ arrays\n"
        "  -help                        Show help\n"
        "Missing input extension: .bmp. Palette output: .txt/.json/.c/.act/.gpl\n"
        "Missing palette extension: append .gpl. -makepal: .gpl/.json only.\n"
        "JSON: [[r,g,b],...]. C: {0xRRGGBB,...}.\n";
}
int run(const std::vector<std::wstring>& args) {
    if (args.empty() || (args.size()==1 && args[0]==L"-help")) {help();return 0;}
    if (args.size()<2) throw std::runtime_error("Missing input. Use -help.");
    auto command=args[0];
    if (command!=L"-info" && command!=L"-pal" && command!=L"-makepal" && command!=L"-redu" && command!=L"-code") throw std::runtime_error("Unknown command. Use -help.");
    fs::path output,palettePath; bool hasOutput=false,hasPalette=false;
    for (size_t i=2;i<args.size();i+=2) {
        if (i+1>=args.size() || args[i+1].empty()) throw std::runtime_error("Missing option value.");
        if (args[i]==L"-o" && !hasOutput) {output=args[i+1];hasOutput=true;}
        else if (args[i]==L"-p" && !hasPalette) {palettePath=args[i+1];hasPalette=true;}
        else throw std::runtime_error("Unknown or duplicate option.");
    }
    if (command==L"-code") {
        if (hasPalette) throw std::runtime_error("-p is not supported for -code.");
        auto files=inputs(args[1]);
        bool batch=fs::path(args[1]).filename().wstring().find_first_of(L"*?")!=std::wstring::npos;
        if (batch && hasOutput) fs::create_directories(output);
        int failures=0;
        for (const auto& file:files) try {
            fs::path target=file;target.replace_extension(L".cpp");
            if (hasOutput) target=batch?output/target.filename():output;
            exportCode(file,target);
        } catch (const std::exception& e) {std::wcerr << file.wstring() << L": ";std::cerr << e.what() << '\n';++failures;}
        return failures?1:0;
    }
    if (command==L"-redu") {
        if (!hasPalette) throw std::runtime_error("-redu requires -p palette.gpl/json.");
        auto palette=loadPalette(palettePath);
        auto files=inputs(args[1]); // Snapshot before writing any files.
        fs::path dir=hasOutput?output:fs::current_path();
        fs::create_directories(dir);
        int failures=0;
        for (const auto& file:files) try {reduce(file,dir,palette);} catch (const std::exception& e) {std::wcerr << file.wstring() << L": ";std::cerr << e.what() << '\n';++failures;}
        return failures?1:0;
    }
    if (hasPalette || (command==L"-info" && hasOutput)) throw std::runtime_error("Option is not supported for this command.");
    auto input=bmpPath(args[1]);auto m=readImage(input);
    if (command==L"-info") {
        if(m.png) {
            std::cout << "Format: PNG\nFile size: " << m.size << " bytes\nWidth: " << m.width
                << " px\nHeight: " << -m.height << " px\nOrientation: top-down\nBits per pixel: "
                << m.sourceBits << "\nPalette colors: " << m.palette.size() << '\n';
            return 0;
        }
        std::cout << "File size: " << m.size << " bytes\nHeader file size: " << m.declared
            << " bytes\nWidth: " << m.width << " px\nHeight: " << (m.height<0?-m.height:m.height)
            << " px\nOrientation: " << (m.height<0?"top-down":"bottom-up")
            << "\nBits per pixel: " << m.bits << "\nMaximum colors: " << (1ULL<<m.bits)
            << "\nPalette colors: " << m.palette.size() << "\nDIB header size: " << m.header
            << " bytes\nCompression: " << m.compression << "\nPixel offset: " << m.offset
            << " bytes\nImage data size (header): " << m.imageSize << " bytes\n";
    } else if (command==L"-makepal") {
        auto palette=makePalette(input,m);
        if (!hasOutput) {output=input;output.replace_extension(L".gpl");}
        savePalette(input,output,palette,true);
    } else {
        if (m.palette.empty()) throw std::runtime_error("This image has no indexed palette. Use -makepal to generate one.");
        if (hasOutput) savePalette(input,output,m.palette);else textPalette(std::cout,m.palette);
    }
    return 0;
}
int wmain(int argc,wchar_t* argv[]) {
    try {return run(std::vector<std::wstring>(argv+1,argv+argc));}
    catch (const std::exception& e) {std::cerr << "Error: " << e.what() << '\n';return 1;}
}
