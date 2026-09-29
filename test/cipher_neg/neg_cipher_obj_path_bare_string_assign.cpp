// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Mismatch class 1 of 2 for Cipher::obj_path: a bare std::string cannot
// take the place of the path that the function returns,
// fixy::Tagged<std::string, tags::source::CipherPath>.
//
// The CipherPath tag says that Cipher built the bytes from its sanitized
// root and a hex content hash, so they never crossed an untrusted
// boundary.  The constructor of Tagged from its value is explicit and
// private, so mint_tagged is the one door.  If this fixture compiles, any
// string can pass as a path that Cipher built.
//
// obj_path is private, so the fixture reads its return type by
// reflection.  A change to the signature changes what the fixture checks.
//
// Companion: neg_cipher_obj_path_cross_source.cpp refuses a path under
// tags::source::FromUserPath.

#include <crucible/Cipher.h>

#include <meta>
#include <string>

consteval std::meta::info obj_path_return_type() {
    for (const std::meta::info member :
         std::meta::members_of(^^crucible::Cipher, std::meta::access_context::unchecked())) {
        if (std::meta::is_function(member) && std::meta::has_identifier(member)
            && std::meta::identifier_of(member) == "obj_path") {
            return std::meta::return_type_of(member);
        }
    }
    return ^^void;
}

using CipherObjPath = typename[:obj_path_return_type():];

int main() {
    CipherObjPath slot = ::fixy::mint_tagged<::fixy::tags::source::CipherPath>(std::string{"/cipher/objects/00/zero"});

    // The compiler must reject this line: no assignment takes a bare std::string.
    slot = std::string{"/some/external/path"};
    return 0;
}
