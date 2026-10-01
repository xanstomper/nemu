#include "core/loader/xci.hpp"
#include "core/crypto/aes.hpp"
#include "core/crypto/key_store.hpp"
#include "core/types.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
using namespace nemu;
static void Hex(const std::vector<u8>& d,size_t o,size_t n){
  for(size_t i=0;i<n;i+=16){ std::printf("  %04zX ",o+i);
    for(size_t j=0;j<16&&o+i+j<d.size();++j) std::printf("%02x ",d[o+i+j]);
    std::printf("\n"); }
}
int main(int argc,char**argv){
  core::crypto::KeyStore ks; ks.LoadFromFile("games/prod.keys"); ks.LoadFromFile("games/title.keys");
  auto hk=ks.GetHeaderKey();
  std::ifstream f(argv[1],std::ios::binary);
  f.seekg(0,std::ios::end); auto sz=(size_t)f.tellg(); f.seekg(0);
  std::vector<u8> rom(sz); f.read((char*)rom.data(),sz);
  core::loader::XciArchive cart; cart.Initialize(rom);
  std::vector<core::loader::XciPayload> pl; cart.UnpackGame(pl);
  for(const auto& p: pl){
    if(p.name.find(".cnmt.")!=std::string::npos||p.name.find(".nca")==std::string::npos) continue;
    if(p.data.size() < 0x01000000) continue;   // Program-sized NCAs only
    std::vector<u8> h(0xC00,0);
    core::crypto::Aes128 a1(std::span<const u8,16>(hk->data(),16)), a2(std::span<const u8,16>(hk->data()+16,16));
    a1.DecryptXts(p.data.subspan(0,0xC00),h,a2,0,0x200);
    if(std::memcmp(h.data()+0x200,"NCA3",4)) continue;
    std::printf("=== %s (%zu) ===\n",p.name.c_str(),p.data.size());
    std::printf("0x204: dist=%d ct=%d keygen=%d kaek=%d keyblob=%d crypto=0x%02x\n",
      h[0x204],h[0x205],h[0x206],h[0x207],h[0x208],h[0x209]);
    std::printf("content_size=0x%llx\n",(unsigned long long)*(uint64_t*)&h[0x220]);
    std::printf("\n-- section_ctrs 0x280 --\n"); Hex(h,0x280,0x40);
    std::printf("-- key_area 0x2C0 --\n"); Hex(h,0x2C0,0x40);
    std::printf("-- ext hdr 0x300 --\n"); Hex(h,0x300,0x40);
    std::printf("-- SEGMENT REGION TABLE 0x400 --\n"); Hex(h,0x400,0x100);
    return 0;
  }
  return 1;
}
