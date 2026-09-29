// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Mismatch class 2 of 2 for Cipher::obj_path: a path under
// tags::source::FromUserPath cannot take the place of the path that the
// function returns, which holds tags::source::CipherPath.
//
// A FromUserPath value came from the command line or from argv.  Cipher
// builds each CipherPath value from its sanitized root and a hex content
// hash.  The two tags wrap the same std::string, but they are
// different types.  The retag catalog lets FromUserPath go only into
// Sanitized, and no tag reaches CipherPath: Cipher mints it with its own
// helpers.  If this fixture compiles, a path from the user can reach the
// openat helpers of Cipher.
//
// obj_path is private, so the fixture reads its return type by
// reflection.  A change to the signature changes what the fixture checks.
//
// Companion: neg_cipher_obj_path_bare_string_assign.cpp refuses a bare
// std::string.

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
    auto user_path = ::fixy::mint_tagged<::fixy::tags::source::FromUserPath>(std::string{"/etc/passwd"});
    CipherObjPath cipher_slot =
        ::fixy::mint_tagged<::fixy::tags::source::CipherPath>(std::string{"/cipher/objects/00/zero"});

    // The compiler must reject this line: the two sources are different types.
    cipher_slot = user_path;
    return 0;
}
