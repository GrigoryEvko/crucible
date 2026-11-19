// Each grade reader of the premises in fixy/Collision.h is a concept or a
// function that is not a template.  L003 reads whether a spawn outlives the
// frame through spawn_outlives_frame_.  This file tries to say that a
// detached spawn ends with the frame, so that L003 admits a borrow across
// it: it writes an explicit specialization of the reader.  The reader is
// not a template, so the file does not compile.

#include <fixy/Collision.h>

template <>
consteval bool fixy::collision::detail::spawn_outlives_frame_(std::meta::info) {
    return false;
}

int main() { return 0; }
