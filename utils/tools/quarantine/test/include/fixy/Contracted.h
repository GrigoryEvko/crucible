#pragma once

// The contract rule applies to the base too: this declaration of a
// function template has no definition, and only the walk of the unit sees it.

namespace probe {

template <class T>
int base_template(T value) pre(value > 0);

}  // namespace probe
