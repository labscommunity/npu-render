#include "ptl_program_ir.hpp"
#include "captured.hpp"
#include <cstdio>
#include <iterator>
using namespace nr::shader;
void test(const char *hash,const uint32_t *bytes,size_t size){try{const auto p=parse_program(bytes,size,Stage::pixel);const auto q=PtlProgram::lower(p);std::printf("{\"pixel_sha256\":\"%s\",\"accepted\":true,\"instructions\":%zu,\"samples\":%zu,\"immediates\":%zu}\n",hash,q.instructions.size(),q.samples.size(),q.immediates.size());}catch(const std::exception &e){std::printf("{\"pixel_sha256\":\"%s\",\"accepted\":false,\"reason\":\"%s\"}\n",hash,e.what());}}
int main(){
test("d8e0b49792a47247020c5b93508846b0b0954403359f83d18e02544d338e8480",pixel0,std::size(pixel0));
test("595296ba14078f6954e7375a47ce1e63b0df241dec67c3569baf4bc16219c2a5",pixel1,std::size(pixel1));
test("0a7b1c6911deb70e4b09b61d682e8d596d59792a11213250d71725c0bd660bd3",pixel2,std::size(pixel2));
test("c71638aa9df382391c81ce814b60b92cff815bf2b31547a5569cfe785beaf341",pixel3,std::size(pixel3));
test("39799859d6531970077cbe235484fb7164778b325f8f58e3b8d6774351baf0b3",pixel4,std::size(pixel4));
test("e3ebf67c53b678eb5b77e36cec775237832c2ce5d606d6b0ef8d44f362447872",pixel5,std::size(pixel5));
test("b92ae2a7745b2cc0a94ec294a23dba3b6f34a1035d78c9751cc28b9bdf038b15",pixel6,std::size(pixel6));
test("4e5ed0f81f4172324fd21bccde5309aff541f82c95c0f51b0690b9441d599932",pixel7,std::size(pixel7));
test("fbe68f57c834b0adb911c186b19a28bb833bdfe10ea91385708ebe228012561c",pixel8,std::size(pixel8));
test("b7f4e6d0abea95bb235098b7e357eacd906679d4626ba4abf4b3c5e3ca31ecf7",pixel9,std::size(pixel9));
test("95b1c4fbb5f9bf0f34ad7b76eca5ae122d956a9570e1796e1bd4aecf43ca7dbe",pixel10,std::size(pixel10));
test("d056c6dcba0a34d02c9f01218f22c4db02daf7082789276afc0c166cb1553772",pixel11,std::size(pixel11));
test("d305ca5e55d0ca0d606b416ed015b566dd4746641b030a44af27f8ffd2777a1a",pixel12,std::size(pixel12));
test("7c5bc4d830ee9593b7f2a8a59c35ae629122a0b657bf7e2b7a2c1bb462b04f8f",pixel13,std::size(pixel13));
test("e455f7a56bb0a371b874f6fbd213edd72a456f711858fc96f9e88631d4cc6951",pixel14,std::size(pixel14));
test("ec1e65f6664c9faf5af829eebdc7799cdde111ce4bad9f730ea5b13878546804",pixel15,std::size(pixel15));
test("20e56de0447ed62eeb48ce9d442b806d3afb2d3c6fb6a2674aad53d98bd03c69",pixel16,std::size(pixel16));
test("3e6229d456bf2f15688551a8ce0024ef8ee019b7a4a496929eac9bedc9c373e3",pixel17,std::size(pixel17));
test("2a28d3fe3b08585f199e86d5aa500f109761b3e4c5eaafa1730bba3eed7ee0a8",pixel18,std::size(pixel18));
test("691da1da1f77b5c9e433e7540401fc748700d351e65e7520350bef66e0f3ba2a",pixel19,std::size(pixel19));
test("c58da1994be91ca8ff229b25c20d3ae53488b1db8cd60972d5d1941970d9afb8",pixel20,std::size(pixel20));
test("0daddc2311bec2f92d461ec657f6ed418b6b250c89041234548c546a40eaaf36",pixel21,std::size(pixel21));
test("5a764502f81741e8cf27e849025854eea948cbfe1a193695cd40e43089577e78",pixel22,std::size(pixel22));
test("7823741aa26b80ab1d183a947f93575665d4dd6a334aba98427ae3a142b25ae4",pixel23,std::size(pixel23));
}
