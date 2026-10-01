#pragma once

// The contract rule applies to the substrate too: this declaration of a
// function template has no definition, and only the walk of the unit sees it.

namespace probe {

template <class T>
int substrate_template(T value) pre(value > 0);

}  // namespace probe
