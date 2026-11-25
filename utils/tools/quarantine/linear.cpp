// The family linear: the rule must-consume and the six rules of the shapes.
// Each path of a body uses a linear value exactly one time
// (misc/12_10_2026_linear.md, P1).  The rule must-consume reads the C++
// statement tree of each body at PLUGIN_PRE_GENERICIZE, before cp_genericize
// lowers it.  It walks the paths of the body forward, with the states dead,
// live, consumed and maybe for each object, and with a fixed point for each
// loop.  It runs only with the argument must-consume=on of plugin.cpp, and the
// rules of the shapes run only with the argument linear-shapes=on.
//
// Linear types
//     The marker of include/foundation/Linear.h decides, as the trait
//     is_linear_type does.  A class or a union is linear when it carries the
//     P3394 annotation foundation::linear::linear.  GCC keeps an annotation as
//     an attribute that annotation_p() accepts, and the value of the attribute
//     has the type of the annotation.  So an instance of linear_when<false>
//     carries not_linear and is not linear by its annotation.  A class that
//     can move and holds a linear base, a linear member or an array of linear
//     elements by value is linear too, also a closure with a linear capture.
//     A reference and a pointer hold no value.  The end of the scope of a value
//     of a class that carries scope_end_door is a door, because the destructor
//     of the class releases its resource, unless a part of the class is or
//     holds a linear sum (the sum holders).  So is the end of the scope of a
//     value whose linear parts each have such a class.  The rule reads whether
//     a class can move from the constructors that the class declares, and it
//     declares no implicit member and instantiates nothing.
//
// Objects
//     The rule tracks a linear local, a structured binding of a linear type, a
//     parameter of a linear class by value and a parameter of an rvalue
//     reference to a linear class.  It tracks a parameter of a reference that
//     is not const to a linear class, and the object of a plain member
//     function, as a lent object (the lent parameters).
//     A local reference that extends the life of
//     a linear temporary, such as T&& name = T{}, auto&& name = make() or
//     T const& name = make(), and the hidden range reference of a range-based
//     loop over a temporary, name that temporary, so the rule tracks the
//     reference as the object, and the end of its scope is the end of the
//     temporary.  It does not track a static or a
//     thread-local variable, a capture, the hidden variable of a structured
//     binding, or the explicit object parameter of a member function.  It
//     tracks no object of a move constructor, of a move assignment, or of a
//     constructor or an assignment of a class that carries the annotation,
//     because these functions define the transfer.  Only the walk of the
//     summary of a call operator tracks a capture (the summaries).
//
// Consumes
//     An object is consumed when it reaches one of these places, through
//     casts, std::move, std::forward and the reference helpers only.  A
//     reference helper is a function whose each return gives back one of its
//     parameters by an lvalue reference, such as through(T&) of a test.  A
//     helper with no body in the unit, or with its body after the call, names
//     no argument (control-known-gaps):
//       * an argument of a parameter of an rvalue reference or of a class by
//         value, of a call that is not std::move or std::forward
//       * the object argument of a member function with the qualifier &&,
//         also of a member of a base class of the object (the base doors)
//       * the value of a return
//       * the source of an initialization or an assignment of a linear
//         object, also of a member, and an element of a braced list, such as
//         a capture of a lambda.
//     A call of a function that does not return ends its path, and so does a
//     throw.  The rule reads no exception path, because the tree does not
//     throw (CLAUDE.md section XII).
//
// Folds
//     The front end replaces an initializer whose value is a constant with
//     that value.  A move of an empty token gives a constant, so the fold
//     removes the consume from the tree.  The fifth patch of GCC
//     (utils/toolchain/gcc/patches/0005-*.patch) gives the event
//     PLUGIN_FOLDED_INITIALIZER with the initializer before the fold, and the
//     rule reads that tree in place of the folded tree.  The front end also
//     replaces a trivial assignment of an empty class with its source, and the
//     event PLUGIN_EMPTY_CLASS_ASSIGNMENT gives the target of the assignment.
//     A source with a side effect stays in the tree, and the rule finds the
//     assignment there.  The front end removes a source with no side effect,
//     and the statement keeps a void constant.  The rule then reads each such
//     event whose location is in the statement: after the start of the
//     statement and before the start of the next statement.  The front end
//     collects no garbage between such an event and the PLUGIN_PRE_GENERICIZE
//     of its function, and each collection clears the records of the events.
//
// Dropped elements
//     The front end folds an element of a braced list that moves an empty
//     token to an empty constant.  It then drops the initialization of an
//     empty field from the CONSTRUCTOR, so the tree keeps no sign of the move,
//     as in Record tokens{std::move(first), std::move(second)} of two tokens.
//     The event of the fold of an element gives the move.  An element whose
//     folded tree the body, with the originals of its folds, does not hold is
//     dropped.  The rule gives the move of a dropped element to one statement
//     of the body: the declaration, the expression statement or the return
//     with the last start at or before the location of the move.
//
// Doors
//     A door ends each linear value of its frame (P10 of the plan).  A
//     function that carries the annotation drop_door, detach_door or once_door
//     is a door, and so is each function that a linear-door row of the rule
//     table names.  The once door call_once of include/foundation/Linear.h
//     owns a closure and calls it one time, so a call of the door runs the
//     summary of the call operator of the closure (the summaries).
//     The rule gives no finding unconsumed, unconsumed-on-path,
//     discarded-temporary or borrow-move-out in the body of a door, apart from
//     the end of a value that is or holds a linear sum (the sum holders).  It
//     still gives double-consume and assign-over-live there.  It gives the record door for
//     each linear-door row whose function it reads, and the module of the rule
//     refuses a row with no record (the rule table is the roster of the doors).
//
// Bodies
//     The rule reads each body of a quarantined file in each unit.  It reads a
//     body of a base file that is no instance of a template only in the
//     sentinel unit of that file (the sentinel of plugin.h), and an instance
//     of a template in each unit that makes it.  It skips a clone of a
//     constructor or a destructor, because GCC gives a clone a second event on
//     a body that cp_genericize lowered.  It skips each body of a unit with an
//     error.  A parse and an object compile read the same trees.
//     is_body_read_here of plugin.h gives these bodies, as it gives them to
//     the rules binding-hook and borrow-match and to the closed tags.
//
// Kinds
//     unconsumed          the object leaves its scope at an exit, and no
//                          path to that exit consumes it
//     unconsumed-on-path   the object leaves its scope at an exit, and only
//                          some paths to that exit consume it
//     double-consume       a consume of an object that a path consumed
//     assign-over-live     an assignment to an object that holds a value that
//                          a path did not consume: the old value drops
//     self-move            a move assignment of an object from itself (the
//                          self-moves)
//     discarded-temporary  a linear temporary that the code discards, or binds
//                          to an lvalue reference and then destroys
//     unanalyzed           a function with a goto or a label, which the rule
//                          does not read
//     door                 no finding: a linear-door row that the unit read
//     option-unconsumed    a linear sum that can end with its alternative, on
//                          each path or on some paths (the sums)
//     option-discarded     a temporary of a linear sum that the code discards
//                          (the sums)
//     option-known-full    a door that needs an empty sum, such as fill or
//                          expect_none, on a sum that each path shows full
//                          (the sums)
//     option-known-empty   a door that needs a full sum, such as expect, on a
//                          sum that each path shows empty (the sums)
//     option-static        an object of static or thread storage duration that
//                          is or holds a linear sum (the sum holders)
//     borrow-move-out      a consume of a lent object that a path keeps to
//                          an exit (the lent parameters), and a move out of
//                          a place that no frame of the function owns (the
//                          moves through a borrow)
//     closure-move-out     a call of a closure that moves a linear capture
//                          out, outside a once door (the closures that move a
//                          capture)
//     capture-unconsumed   a closure that a call owns and whose call operator
//                          leaves a linear capture by value live on a path
//                          (the closures that a call owns)
//     The entity is FUNCTION: OBJECT (TYPE), and FUNCTION: temporary (TYPE)
//     for a temporary.  FUNCTION has no template argument.  TYPE is the type
//     of the object or the temporary with its template arguments, and each
//     fresh brand in it is the word "brand" (linear_type_text).  So each
//     instance of one template that ends one type at one line gives one line,
//     an instance that ends a different type gives its own line, and the
//     ledger counts each type.  The key of a ledger row holds no line of an
//     exit.  The entity of unanalyzed is FUNCTION, and the entity of door is
//     the function of its row.  The TYPE of closure-move-out is the type of
//     the closure (the closures that move a capture).  The entity of
//     capture-unconsumed is FUNCTION: closure (TYPE), with the function that
//     holds the lambda and the type of the closure (the closures that a call
//     owns).
//
// Fields
//     An object of a class that is linear by its parts gets one slot for each
//     linear field.  The fields of a base that is linear by its parts count as
//     fields of the object, as the parts of a Record in the cells of its bases
//     do.  The object gets no field slot when a base carries the annotation
//     linear, or when two paths of bases reach one field.  A move of a field
//     consumes its slot, and the move of the last field consumes the object.
//     The field of a move is the innermost field that is no base on the way
//     from the move to the object.  A move of the whole after a move of a
//     field, or of a field after a move of the whole, is a second consume.  A
//     reference to a field borrows it.  In the body of a door, a holder of a
//     linear sum gets a slot only for each field that is or holds a sum (the
//     sum holders).
//
// Sums
//     A linear sum is a linear class that carries the annotation sum of
//     include/foundation/Linear.h: an Option, a Result or a OneOf of a linear
//     alternative.  It gives the alternative out only through a consumer.  A
//     tracked object whose own type is a linear sum gives the kind
//     option-unconsumed in place of unconsumed and unconsumed-on-path, and a
//     temporary of one gives option-discarded in place of
//     discarded-temporary.  Each finding of the two kinds is a compile error
//     in each build and each mode (is_each_mode_kind of plugin.h).  The
//     message does not start with "quarantine: ", so a regex of a negative
//     fixture reads it.  A field of a class keeps the other kinds.
//     The content of a sum is a fact beside its status: empty, or can be
//     full.  A sum is empty at a point only when each path to it shows it
//     empty, so each new value, each parameter and each value of a call can
//     be full.  These facts show it empty:
//       * a constructor with the annotation empties built it, such as the
//         default constructor of an Option, through the original of the folds
//       * a member with the annotation empties ran on it, such as take
//       * a consumer took it, because a consume moves the alternative out
//       * a test with the annotation tests_empty or tests_full in the
//         condition of an if statement said empty on the branch, also through
//         ! and through an arm that does not return, as the assert of
//         fixy/check/Check.h does.
//     A member with the annotation fills, such as fill, gives the sum a new
//     value to consume, and shows it full.  A builder with fills, such as
//     some, builds a full sum, and a test that says full shows it full too.
//     A sum is full at a point only when each path shows it full.
//     The known content: a member with the annotation needs_empty, such as
//     fill and expect_none, on a sum that the rule shows full gives the kind
//     option-known-full, and a member with needs_full, such as expect, on a
//     sum that the rule shows empty gives option-known-empty.  The run-time
//     check of such a member would end the process on each path.  The check
//     stays only where the rule does not know the content: a sum of a call, a
//     parameter, a member, a sum after an alias or a join of two contents,
//     and a sum after a call that took it by a reference or a pointer that
//     is not const, because the callee can keep the reference and fill the
//     sum at a later call.
//     A use that can fill the sum makes it one that can be
//     full: a member with an object parameter that is not const, and an
//     argument of a reference or a pointer that is not const.  An alias of
//     the sum can fill it at each later point: a capture by reference, an
//     address that is not const in a pointer, and a reference member that is
//     not const.  So after an alias, no fact shows the sum empty or full.
//     A consume ends it only up to the next call that can fill it (the
//     summaries, the aliases).  A capture hides each later consume, so a
//     captured sum that can be full at its end gives the kind
//     option-unconsumed.  An empty sum at an exit drops nothing.  A Result
//     and a OneOf have no member with these annotations, so only a consume
//     ends one.  A sum that a holder holds ends with the holder (the sum
//     holders).
//
// Sum holders
//     A linear sum ends only through its consumers, so no door ends one.  A
//     holder of a sum is a value whose type is or holds a linear sum by value:
//     through the element of an array, a base, a data member that is no
//     reference, or a type argument of a linear class, such as the payload of
//     a Box or the element of a list (holds_linear_sum).  The drop door and
//     the door end_unrun_body of include/foundation/Linear.h refuse a holder
//     by its type (states_own_end).  The rule reads three more ends of a
//     holder:
//       * The body of a door keeps the kinds of a drop for an object or a
//         temporary of a holder: option-unconsumed and option-discarded for a
//         sum, and unconsumed, unconsumed-on-path and discarded-temporary for
//         each other holder.  Such an object gets one field slot for each of
//         its own fields that is or holds a sum, also when its class carries
//         the annotation (the fields).  So a door that consumes each such
//         field, such as through an rvalue match, ends the holder with no line.
//       * The end of the scope of a class with scope_end_door is no door when
//         a part of the class is or holds a sum (the linear types), because
//         its destructor ends that sum, and the rule reads no destructor.  So a
//         destructor that consumes its sum still gives a line
//         (control-known-gaps).
//       * An object of static or thread storage duration that is a holder
//         gives the kind option-static, an error in each mode, at its
//         declaration: a variable at namespace scope, a static data member and
//         a static or thread_local local (check_static_linear_sum, from the
//         static walks of declaration.cpp).  The end of the process or of the
//         thread ends it, and no path of a body reaches that end.  A variable
//         whose type names a template parameter is read in no instance.  A
//         static linear value that holds no sum, such as a token, gives no
//         line of this kind.
//     A named end door of a sum, which takes the reason of the end, and which
//     the drop door, the body of a door and a destructor call for each sum
//     that they end, retires the first two lines: the rule then reads each
//     such end as a call of that door.  C++ states no storage duration in a
//     type, so no type of the base retires the kind option-static.
//
// Lent parameters
//     A parameter of a reference that is not const to a linear class is a
//     lent object: the caller keeps the value, and the function borrows it.
//     So a move out of it leaves the caller a spent value, which looks the
//     same as a live one.  A later move of the caller then gives a second value
//     of one value, such as a second exclusive token of one region.  Rust
//     refuses a move out of a borrow, and mem::replace and mem::take are its
//     only moves out of one.  The rule walks a lent object as each other
//     object, live at the entry.  A path may consume it, but each path must
//     give it a value again before it leaves the function: an assignment to
//     the object, or to each field that a path consumed.  The kind
//     borrow-move-out is at the first consume of a lent object that some path
//     keeps consumed to an exit.  The exchange door of
//     include/foundation/Linear.h is the form of mem::replace: it gives the old
//     value back and leaves its replacement in the object.  A structured
//     binding by reference of a lent object borrows it, so each of its
//     bindings is a lent object until the end of its scope.  A lent object owes
//     no consume at its end.  An assignment over a live lent object drops the
//     value of the caller, so it gives the kind assign-over-live, and an
//     assignment after a consume gives no line.  The rule drop-door reads no
//     assignment over a lent parameter when this rule runs (the shapes).  A
//     capture by reference of a lent object hides each later consume (the
//     captures).  The body of a door, which ends a value in its place, gives no
//     line borrow-move-out (the doors).
//     The object of a plain member function is a lent object too, through its
//     implicit this: a function that is no constructor, no destructor and no
//     call operator of a closure, with no qualifier && and an object that is
//     not const (is_lent_this).  A move of a linear member out of *this then
//     gives borrow-move-out, because a second call would give a moved-from
//     member.  A member function that gives up its object states &&, and its
//     object is no lent object.
//
// Moves through a borrow
//     A move can name a place that no tracked object holds: a reference that
//     a function gives back, a dereference of a Ref or of a pointer, a local
//     lvalue reference, or a part of an object that the rule does not track.
//     Two such moves give two values of one value, as two moves of
//     token_of(holder) do.  Rust refuses a move out of a borrow.  So at a
//     consume that names no tracked object (an argument of a parameter && or
//     by value, the object of a member function &&, a declaration, an
//     assignment, a return and an element of a braced list), a move of a
//     linear lvalue gives the kind borrow-move-out when no frame of the
//     function owns its place (borrowed_place).  The walk goes through the
//     casts, std::move, the reference helpers, a member, an element and a
//     binding of a structured binding, and from a local lvalue reference to
//     its initializer.  A call that gives a reference names a part of its
//     object, or of its first argument that refers to a class, when that
//     class is linear.  Each other call that gives a reference, such as
//     operator* of a Ref, names a place of another frame.
//     These places are owned, and give no line: a temporary, a local or a
//     parameter by value or by an rvalue reference, a reference that extends
//     a temporary, a capture, and the object of a lambda, of a constructor,
//     of a destructor, of a member function && and of an explicit object
//     parameter by value or by an rvalue reference.  A tracked object is
//     owned when it is no lent object and the walk does not reach it behind a
//     call or a local reference: the rule consumes no part of the object for
//     such a move, so a second move would give a second value.  A transfer
//     function gives no line, and neither does the body of a door (the
//     doors).
//     The base moves a payload out of its own storage and out of its erased
//     frames.  The annotation moves_own_storage of include/foundation/Linear.h
//     states it, and the rule trusts it only on a function of a base file.  A
//     call of such a function on the object of the function of the walk, or
//     of a static one in a member of its own class, names a place that the
//     base owns, such as the payload of an Option, also through the pointer
//     that it gives, such as a slot of a list (is_own_storage_call).  The
//     same call on another object names a place of another frame.  A move in
//     the body of such a function gives no line, such as the take of the
//     body of a thread from its frame.  A function outside the base with the annotation keeps its
//     lines (plant-own-storage-outside).
//
// Base doors
//     A member function of a base class gets the base subobject as its object.
//     The qualifier && of such a member tells that the call takes the object,
//     as the detach of the base of a session handle does.  So the walk from
//     the object argument of such a call follows each step to a base back to
//     the tracked object, and the call consumes the whole object and each of
//     its field slots.  The rule trusts the member, as it trusts a member with
//     the qualifier && of the class itself.  A base is no field slot, so a move
//     of a base subobject into a parameter consumes nothing, and the object
//     stays live.
//
// Accessors
//     After a move cast, the walk to the tracked object goes through a member
//     function with no qualifier && that gives an lvalue reference to a linear
//     value, such as operator* or value() of std::optional.  The rule reads
//     the return statements of the body of each such accessor at its own
//     PLUGIN_PRE_GENERICIZE.  When each one gives the same field of *this, the
//     move of the reference consumes that field only.  Else the move consumes
//     the object.  An accessor whose body the unit genericizes after the
//     caller, such as a member template with a declared return type, has no
//     such fact, and its move consumes the object.
//
// Captures
//     A lambda initializes a capture by reference with the address of the
//     object.  The closure hides each later consume, so the object gives no
//     finding, and the rule linear-capture reports the capture.  A loan key,
//     a linear class with the annotation loan_key_mark of
//     foundation/core/Frame.h, owes no consume: the end of its scope is its
//     door.  A nested frame of two keyed owners captures the key of the inner
//     owner by reference, so a capture by reference of a loan key is no
//     finding.  A capture by const reference reads the object and cannot
//     consume it, because an rvalue member and a move constructor do not bind
//     to a const object.  So it is no finding, unless the class has a door
//     that consumes a const object: a member function with the qualifier
//     const&&, or a mutable field, in the class, a base or a member that it
//     holds by value (consumes_through_const).  Each other linear class stays
//     a finding, also a class with a door at the end of its scope.
//
// Summaries
//     Three facts of a body serve each later caller in the unit, at the
//     PLUGIN_PRE_GENERICIZE of the body:
//       * the capture fields of a closure whose sum the body of its call
//         operator can fill: an argument of a parameter that writes it, other
//         than the object of a consumer or of a member that empties, and the
//         target of an assignment (note_closure_fills)
//       * the capture fields of a call operator whose sum each path of its
//         body leaves consumed or empty.  A walk of the body in the form of
//         the summary gives it: the walk tracks the proxy of each capture by
//         reference of a linear sum, which the closure borrows, and its end
//         gives no finding (note_closure_empties)
//       * a function whose body keeps no reference parameter that names a
//         linear sum: each use of such a parameter is the object of a member
//         call, or an argument of a parameter that does not write, or of a
//         callee that keeps no reference (note_kept_references).  A body that
//         only names a reference needs no complete class, so a parameter of
//         an incomplete class counts as one that can name a sum.
//     The call of a call operator consumes each sum that its summary leaves
//     empty, and so does a call of the once door that takes the closure.  A
//     call of a closure, and a call of each other callee that takes it, can run
//     its body.  So after such a call, each other sum that the closure
//     captures by reference and that its summary names holds a payload
//     again, also after a consume.  A closure with no summary can fill each
//     such sum, and a closure that a callee takes can fill it at each later
//     call too (the aliases).  A call that takes a sum by a reference or a
//     pointer that writes, and whose callee can keep the reference, makes an
//     alias of the sum.
//     A callee whose body the unit genericizes after the caller, such as a
//     template that the end of the unit instantiates or a function below the
//     caller, has no fact at the walk of the caller.  So a body that reads
//     such a callee gets two walks: one where the callee can keep the
//     reference, and one where it keeps none.  When the two walks give other
//     findings, the findings wait for the end of the unit.  Then each such
//     callee has its fact, or it has no body in the unit and can keep the
//     reference, and the rule adds the findings of the walk that the facts
//     give (judge_deferred_findings).  The fact of a body that gives a sum
//     to such a callee waits in the same way, and the end of the unit gives
//     each waiting fact by a least fixed point, so a recursion keeps the
//     reference.  The facts are keyed by a UID (the collections of plugin.h).
//
// Aliases
//     A stored address that is not const, a reference member that is not
//     const, a callee that can keep a reference, and a closure that a callee
//     takes can name a tracked sum beside the name of its object.  A write
//     through such an alias needs a call, so each call that starts after the
//     consume of the sum can fill it again, and the sum then holds a payload
//     that the end of its scope drops.  The call of a member of a linear sum
//     on a tracked object changes only that object, a consumer, a member
//     that empties and a const member fill no sum, and std::move changes
//     nothing.  A callee whose body gives a reference parameter back as its
//     result, and keeps it in no other way, keeps no reference: only a
//     reference or a pointer local that its result initializes is an alias,
//     and a member that can fill a sum fills it through that result.  The
//     callee of the call that takes the consumed payload starts
//     before the consume ends, so the rule reads it as no such call.  A
//     capture by reference is no such alias, because the summary of the
//     closure tells what each call of it does.
//
// Closures that move a capture
//     A call operator of a lambda has no qualifier &&, so each call of a
//     closure reads its captures as members of a borrowed object.  A call
//     operator with an explicit object parameter that is no rvalue reference,
//     such as this auto& self, borrows its closure the same way
//     (calls_moving_closure).  A body
//     that consumes a linear capture, or a linear field of one, leaves a
//     valid moved-from value in the closure.  So a second call gives a second
//     value of one capture, such as a second exclusive token of one
//     permission, and no type and no run-time state refuses it.
//     The summary walk of the call operator gives the fact: a path to an exit
//     leaves a linear capture, by copy or by a reference that is not const,
//     or one of its fields consumed (note_closure_summary).  A walk with a
//     goto fails closed.
//     A call of such a closure, and a call of a function with the annotation
//     once_door that takes such a closure, needs an object that the frame
//     gives up: a temporary closure, or an rvalue of a local or of a
//     parameter by value or by an rvalue reference (is_object_given_up).
//     The full expression ends a temporary, and the rule use-after-move
//     refuses a later use of a name that a cast spends.  Each other such call
//     gives the kind closure-move-out, such as a call of a closure that a
//     door borrows, or a call of the once door with a closure that a
//     parameter lends.  The once door call_once of include/foundation/Linear.h
//     moves the closure into its frame and calls an rvalue of it, so its own
//     call meets the test.
//     A call operator that gives a capture of its own closure to the once
//     door gives no line there: the closure then moves a capture out too, so
//     each call of that closure meets the test (check_closure_calls).
//     The entity is FUNCTION: OBJECT (TYPE), with the name of the object of
//     the closure or the word closure, and the type of the closure.
//     The kind is a counted line of the ledger (the counted lines of
//     plugin.cpp): a directory of the enforce mode error with no count row of
//     the kind refuses it at compile time.
//
// Closures that a call owns
//     The once door call_once of include/foundation/Linear.h moves a closure
//     into its frame, calls it one time and ends it.  Such a closure owns its
//     captures by value, as a function owns its parameters by value.  So a
//     linear capture by value that the call operator leaves live on a path
//     drops with the closure, and no caller wrote that drop.  The summary
//     walk gives the fact for each closure: a linear capture by value whose
//     slot a path to an exit leaves live, or that the body never names
//     (note_closure_summary).  A walk with a goto fails closed.  Each call
//     of the once door that takes such a closure gives the kind
//     capture-unconsumed at the place of the lambda, so the line is in the
//     code of the caller also when the call is in a door of the base, such as
//     the call of a body of run_joined.  A temporary closure ends at the end
//     of its full expression.  A call of a temporary closure whose call
//     operator ends each linear capture on each path drops nothing, so it
//     gives no line.  Each other linear temporary closure, discarded or
//     called, gives the kind discarded-temporary, and no line of this kind.
//     The entity is FUNCTION: closure
//     (TYPE), with the function that holds the lambda and the type of the
//     closure.  A capture by reference stays with the caller, so it gives no
//     line.  The kind is a counted line of the ledger.  A closure type whose
//     captures the rule must-consume reads, as it reads the parameters of a
//     function, retires these lines, and C++ cannot state such a type today.
//
// The generic closures
//     A generic lambda, and a lambda with an explicit object parameter, has a
//     call operator template.  The summary walk reads each instance of it,
//     and lambda_function gives only the template.  So the two facts of the
//     closures also have the TYPE_UID of the closure as their key: a closure
//     type moves a capture out, or leaves a capture by value live, when one
//     instance of its call operator does.  A call of the once door whose
//     closure has a linear capture by value and a call operator with no fact
//     at the walk gives a waiting line.  The door instance can come before
//     the instance of the call operator in the unit, so the end of the unit
//     keeps each waiting line whose closure type leaves a capture
//     (judge_waiting_owned_closures).
//
// The named bodies
//     The once door also takes a class that is no closure as its body, its
//     first argument, and ends it after the call.  The call operator && of
//     such a class owns its object, as a closure owns its captures by value.
//     The summary walk of such a call operator reads the object as an owned
//     body (note_named_body_summary): a path to an exit that leaves the object
//     or one of its linear fields live gives the fact, and a walk with a goto
//     fails closed.  The fact has the TYPE_UID of the class as its key.  A call
//     of the once door whose body has the fact gives the kind
//     capture-unconsumed at the name of the class, with the entity named body
//     (TYPE).  A body whose call operator has no fact at the walk waits for
//     the end of the unit, as a generic closure does.  A body that no summary
//     read by then fails closed with the same line: a class with a plain call
//     operator, which a second call can run again, or with no body of its call
//     operator && in the unit.
//
// Self-moves
//     A move assignment whose target and source are one object is the kind
//     self-move, at the assignment: an operator= that takes an rvalue
//     reference to a class, of each class, and an assignment of a linear class
//     that the front end keeps as a MODIFY_EXPR.  A move keeps nothing that
//     its target owes when the target is its source.  A session handle over a
//     Resource that is no linear class is no linear class either, so the
//     operator= form reads each class, as -Wself-move of GCC does for the
//     direct form x = std::move(x).  The two sides name one
//     object when they are equal after the casts, the addresses, the
//     dereferences, std::move and std::forward, and after a local lvalue
//     reference to a plain local or parameter that its initializer names
//     (cp_tree_equal).  A reference through a dereference or an index is no
//     alias, because a later write of the pointer or the index can change the
//     object of the same expression.  A side with a side
//     effect names no object.  So a local, a local through a reference to it,
//     a member, an element at one index and an object behind one pointer or
//     one reference count, also when the rule tracks no object of them.  A
//     self-move moves no value, so the states of the walk stay.  An alias
//     through two names, such as two reference parameters or two indices of
//     one value, is no finding: the rule reads one function at a time, and the
//     move assignment of the class is the run-time backstop.
//
// Limits
//     The rule reads one function at a time.  A consume through a reference or
//     a member call of a door is not visible to it, and a consume that depends
//     on a correlated branch, such as a test of has_value(), needs a switch or
//     a door.  The tests of the empty state of a linear sum are the one such
//     correlation that the rule reads (the sums).  A template that no unit
//     instantiates with a linear type gives no finding.
//
// Shapes
//     The rules of the shapes read each class, each signature and each body
//     of the bodies that must-consume reads (the bodies), with the same places.
//     A class is read at PLUGIN_FINISH_TYPE, and an instance of a template
//     where a declaration or a body names it.  The rules:
//       linear-containment  a class that holds a linear part, cannot move and
//                           has no door at the end of its scope (pinned-holder),
//                           a class that holds a linear part and has a
//                           user-provided copy (copyable-holder), and a
//                           declaration whose type holds a class of the library
//                           with a linear argument (library-holder).  Such a
//                           class is no aggregate, and it is linear by its parts
//                           or it has a destructor that is not trivial
//       linear-capture      a capture by reference of a linear object that is
//                           no loan key
//       drop-door          an assignment over a linear object that is no
//                           tracked object of must-consume, and a destructor
//                           call or a clobber of such an object.  A door, a
//                           destructor and a transfer function read none.
//                           The entity names the type of the object that
//                           ends (linear_type_text), so a new instance that
//                           ends a new type at one line gives its own line.
//                           The end in a member of a union of the base, such
//                           as the alternatives of a sum, names its place and
//                           no type (is_base_union_member): the owner of the
//                           union owes the end of each alternative
//       linear-forge        a union that holds a sealed type, a linear type or
//                           a type that the checked lifetime start of
//                           include/foundation/Lifetime.h refuses, and a class
//                           with the annotation linear that it admits
//       brand-escape        a fresh brand, the closure type of a lambda with no
//                           capture and no parameter in a template argument,
//                           that leaves its scope: a return type, a member, a
//                           static object, an alias outside a body, and the type
//                           of a local container of the library or function
//                           pointer.  A brand that a template argument of the
//                           function or of its class gave is bound (the brands).
//                           A scope brand gives the kinds of the scope brands
//       brand-reentry       a call with an argument of a brand that the caller
//                           made, whose callee can reach the caller again in the
//                           call graph of the bodies of the unit
//     The front end gives no PLUGIN_FINISH_DECL for an alias, so the walk of
//     the declarations of declaration.cpp gives each alias at namespace scope
//     (check_linear_alias).  The rule brand-reentry reports at the end of the
//     walks of the unit (finish_linear_rules).
//
// Brands
//     GCC gives the closure of a lambda in a default template argument the
//     translation unit as its context, so no closure tells which function made
//     it.  A brand is bound at a declaration when a template argument of the
//     declaration, or of a class or a function that holds it, holds the brand.
//     The type of a value argument holds a brand too, such as the parameter of
//     the function of a stage.  Each other brand came from a mint call or a
//     decltype in the scope itself.
//
// Scope brands
//     A scope brand names one call of a scope mint, such as mint_branded_scope
//     of include/foundation/core/Branded.h.  CRUCIBLE_SCOPE_BRAND of
//     include/foundation/Brand.h writes it: a lambda whose call operator
//     carries the annotation foundation::brand::scope_brand, which reflection
//     reads too.  One call site of the mint gives each of its calls one brand,
//     so a value of the brand that outlives one call stands in for a value of
//     the next call.  An index of a long view then reads past the end of a
//     short view.  A value of a scope brand therefore leaves no call of its
//     mint, also when a template argument gave the brand:
//       scope-static        an object with static or thread storage duration
//                           whose type holds a scope brand: a static or a
//                           thread_local local, and a static data member, a
//                           variable template instance or a variable at
//                           namespace scope that a body names
//       scope-return        a scope mint whose result holds its own scope
//                           brand, also in a capture of a closure or a field of
//                           a local class
//     Each finding of the two kinds is a compile error in each build and each
//     mode (is_each_mode_kind of plugin.h).  The message does not start with
//     "quarantine: ", so a regex of a negative fixture reads it.  A value of a
//     scope brand that a type erases, such as a function object that captures
//     an index, holds no brand in its type, and the rule cannot see it.
//
// Arena brands
//     An arena of include/foundation/core/BumpArena.h and a SlotMap of
//     include/foundation/core/SlotMap.h carry the annotation branded_arena of
//     include/foundation/Brand.h, and a type argument of each one is its
//     brand: the first argument of an arena, the third of a SlotMap.  A member
//     arena tag carries arena_member_brand: it names the one owner of the tag
//     that a class holds as a member.  Two owners of one tag
//     would share the brand, and a Ref of one would fit a field that names the
//     other.  An epoch brand carries epoch_brand: EpochOf<Tag> names the bytes
//     of one epoch of an epoch arena, and the end of the epoch ends them.  A
//     carrier of an epoch is a class that states the epoch brand as its member
//     brand_type, and a carrier holds the brand for each check below:
//       arena-member        a member that holds an arena of a member tag in a
//                           different type, such as an Option or a Box of it,
//                           a member of an arena whose tag is no nested class
//                           of the class, a second member of one tag, a nested
//                           member tag with no member, a local or an object
//                           of static or thread storage duration that holds
//                           an arena of a member tag, and an object of static
//                           or thread storage duration that reaches one
//                           through a path that is not const, such as an
//                           owner of the arena.  A body of an epoch with no
//                           capture reaches each such object
//       epoch-member        a member whose type holds an epoch brand, also
//                           through a reference, a pointer or a carrier, in a
//                           class that does not state that brand
//       epoch-static        an object of static or thread storage duration
//                           whose type holds an epoch brand
//     Each finding of the three kinds is a compile error in each build and
//     each mode.  The door with_epoch refuses a result and a capture by a
//     reference that is not const that hold the brand, at compile time.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// ── The linear types ────────────────────────────────────────────────────

enum class Linearity : std::uint8_t {
    none,
    direct,
    derived,
};

// The facts of one type: its linearity, and whether the end of the scope of a
// value of the type is a door (the linear types).
struct TypeFacts {
    Linearity linearity = Linearity::none;
    bool ends_at_scope = false;
};

// The identifiers that the family compares, interned one time.
struct LinearNames {
    tree linear = NULL_TREE;  // the namespace foundation::linear and its class linear
    tree lifetime = NULL_TREE;  // the namespace foundation::lifetime
    tree no_start_over_bytes = NULL_TREE;
    tree brand_type = NULL_TREE;
    tree brand = NULL_TREE;  // the namespace foundation::brand
    tree scope_brand = NULL_TREE;
    // The arena brands: the annotations of an epoch brand, of a member arena
    // tag and of an arena class, and the marks of a borrow of
    // include/foundation/Transfer.h.
    tree epoch_brand = NULL_TREE;
    tree arena_member_brand = NULL_TREE;
    tree branded_arena = NULL_TREE;
    tree transfer = NULL_TREE;
    tree refers = NULL_TREE;
    tree refers_unique = NULL_TREE;
    tree refers_deep = NULL_TREE;
    tree by_arguments = NULL_TREE;
    tree payload_cell = NULL_TREE;
    tree drop_door = NULL_TREE;
    tree detach_door = NULL_TREE;
    tree scope_end_door = NULL_TREE;
    tree forward = NULL_TREE;
    // The sums: the annotation of a sum class, and the annotations of its
    // empty state.
    tree sum = NULL_TREE;
    tree empties = NULL_TREE;
    tree fills = NULL_TREE;
    tree tests_full = NULL_TREE;
    tree tests_empty = NULL_TREE;
    tree needs_full = NULL_TREE;
    tree needs_empty = NULL_TREE;
    // The annotation of the once door (the doors, the closures that move a
    // capture).
    tree once_door = NULL_TREE;
    // The annotation of a storage door or a frame of the base (the moves
    // through a borrow).
    tree moves_own_storage = NULL_TREE;
};

LinearNames linear_names;

// The answers of the type walks, keyed by the main variant of a class.  Each
// collection clears them, because a collection can free a tree and reuse its
// address.
struct LinearCaches {
    std::unordered_map<tree, TypeFacts> facts;
    std::unordered_map<tree, bool> can_move;
    std::unordered_map<tree, bool> holds_sum;
};

LinearCaches linear_caches;

// The rules that the arguments of the unit run (register_linear).
struct LinearMode {
    bool checks_must_consume = false;
    bool checks_shapes = false;
};

LinearMode linear_mode;

// The folds: each folded tree with the object and the initializer before the
// fold, and the target of each trivial assignment of an empty class by its
// source.
struct FoldEntry {
    tree object = NULL_TREE;
    tree original = NULL_TREE;
};

std::unordered_map<tree, std::vector<FoldEntry>> folded_initializers;

struct EmptyAssignment {
    tree target = NULL_TREE;
    tree source = NULL_TREE;
    location_t location = UNKNOWN_LOCATION;
};

std::vector<EmptyAssignment> empty_assignments;
std::unordered_map<tree, std::size_t> empty_assignment_of_source;

// The folds: a fold of an element of a braced list that moves a linear local
// or parameter.  The front end drops the folded element of an empty field from
// its CONSTRUCTOR, so the tree can lose the move (the dropped elements).
struct ElementFold {
    tree original = NULL_TREE;
    tree folded = NULL_TREE;
    tree moved = NULL_TREE;  // the local or the parameter that ORIGINAL moves
    location_t location = UNKNOWN_LOCATION;
};

std::vector<ElementFold> element_folds;

// The accessors: the DECL_UID of the field of *this that the return statements
// of each accessor give, by the DECL_UID of the accessor, or kWholeObject.  No
// collection reuses a DECL_UID, so the facts stay for the unit.
constexpr int kWholeObject = -1;
std::unordered_map<int, int> accessor_fields;

tree strip_conversions(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        if (TREE_CODE(node) != NOP_EXPR && TREE_CODE(node) != CONVERT_EXPR && TREE_CODE(node) != NON_LVALUE_EXPR
            && TREE_CODE(node) != VIEW_CONVERT_EXPR) {
            return node;
        }
        node = TREE_OPERAND(node, 0);
    }
    return node;
}

// True when ATTRIBUTES hold an annotation of the class foundation::SPACE::NAME
// of the vocabulary of include/foundation/Linear.h, of
// include/foundation/ByteSeal.h when SPACE is lifetime, or of
// include/foundation/Brand.h when SPACE is brand, with foundation::linear as
// the default namespace.
bool carries_annotation(tree attributes, tree name, tree space = NULL_TREE) {
    return carries_base_annotation(attributes, name, space != NULL_TREE ? space : linear_names.linear);
}

tree element_type_of(tree type) {
    while (type != NULL_TREE && TREE_CODE(type) == ARRAY_TYPE) {
        type = TREE_TYPE(type);
    }
    return type;
}

// True when a value of the complete class TYPE can move: its declared move
// constructor is not deleted, or the implicit move constructor can move each
// base and each member.  With no move constructor, a declared copy
// constructor that is not deleted moves it.  The walk declares no implicit
// member, so it changes no tree.  A class that holds a linear part has no
// implicit copy constructor that is not deleted, so a class with no move
// constructor and no declared copy constructor cannot move.  Complexity: the
// walk visits each class one time.
bool can_move(tree type, int depth) {
    type = TYPE_MAIN_VARIANT(element_type_of(type));
    if (!CLASS_TYPE_P(type) || !COMPLETE_TYPE_P(type) || depth > kTypeDepth) {
        return true;
    }
    auto found = linear_caches.can_move.find(type);
    if (found != linear_caches.can_move.end()) {
        return found->second;
    }
    // The answer while the walk is open, so that a cycle through a member
    // ends.
    linear_caches.can_move.emplace(type, true);
    bool result = false;
    bool is_decided = false;
    for (ovl_iterator constructor(CLASSTYPE_CONSTRUCTORS(type)); constructor; ++constructor) {
        tree function = *constructor;
        if (TREE_CODE(function) == FUNCTION_DECL && move_fn_p(function)) {
            result = !DECL_DELETED_FN(function);
            is_decided = true;
            break;
        }
    }
    if (!is_decided && CLASSTYPE_LAZY_MOVE_CTOR(type)) {
        result = true;
        if (tree binfo = TYPE_BINFO(type)) {
            for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo) && result; ++index) {
                result = can_move(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), depth + 1);
            }
        }
        for (tree field = TYPE_FIELDS(type); field != NULL_TREE && result; field = DECL_CHAIN(field)) {
            if (TREE_CODE(field) == FIELD_DECL && !DECL_FIELD_IS_BASE(field) && TREE_TYPE(field) != NULL_TREE
                && !TYPE_REF_P(TREE_TYPE(field))) {
                result = can_move(TREE_TYPE(field), depth + 1);
            }
        }
        is_decided = true;
    }
    if (!is_decided) {
        for (ovl_iterator constructor(CLASSTYPE_CONSTRUCTORS(type)); constructor; ++constructor) {
            tree function = *constructor;
            if (TREE_CODE(function) == FUNCTION_DECL && copy_fn_p(function) > 0) {
                result = !DECL_DELETED_FN(function);
                break;
            }
        }
    }
    linear_caches.can_move[type] = result;
    return result;
}

bool parts_hold_linear_sum(tree type, bool is_linear, int depth);

// The facts of a type (the linear types).  An array has the facts of its
// element.  The end of the scope of a class with the annotation
// scope_end_door is no door when a part of the class is or holds a linear sum
// (parts_hold_linear_sum), because the destructor of the class ends that sum,
// and the rule reads no destructor.  Complexity: the walk visits each class
// one time.
TypeFacts facts_of(tree type, int depth = 0) {
    if (type == NULL_TREE || type == error_mark_node || depth > kTypeDepth || !TYPE_P(type)) {
        return {};
    }
    type = TYPE_MAIN_VARIANT(element_type_of(type));
    if (!CLASS_TYPE_P(type) || !COMPLETE_TYPE_P(type)) {
        return {};
    }
    auto found = linear_caches.facts.find(type);
    if (found != linear_caches.facts.end()) {
        return found->second;
    }
    // A class that holds itself through a member is no type, and the answer
    // keeps the walk finite while the answer is open.
    linear_caches.facts.emplace(type, TypeFacts{});
    TypeFacts result;
    tree attributes = TYPE_ATTRIBUTES(type);
    bool is_scope_door = carries_annotation(attributes, linear_names.scope_end_door);
    if (carries_annotation(attributes, linear_names.linear)) {
        result.linearity = Linearity::direct;
        result.ends_at_scope = is_scope_door && !parts_hold_linear_sum(type, true, depth + 1);
    } else {
        bool holds_linear = false;
        bool each_part_ends = true;
        auto add_part = [&](tree part) {
            TypeFacts part_facts = facts_of(part, depth + 1);
            if (part_facts.linearity != Linearity::none) {
                holds_linear = true;
                each_part_ends = each_part_ends && part_facts.ends_at_scope;
            }
        };
        if (tree binfo = TYPE_BINFO(type)) {
            for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
                add_part(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)));
            }
        }
        for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
            if (TREE_CODE(field) == FIELD_DECL && !DECL_FIELD_IS_BASE(field) && TREE_TYPE(field) != NULL_TREE
                && !TYPE_REF_P(TREE_TYPE(field))) {
                add_part(TREE_TYPE(field));
            }
        }
        if (holds_linear && can_move(type, 0)) {
            result.linearity = Linearity::derived;
            result.ends_at_scope = (is_scope_door && !parts_hold_linear_sum(type, true, depth + 1)) || each_part_ends;
        }
    }
    linear_caches.facts[type] = result;
    return result;
}

Linearity linearity_of(tree type) { return facts_of(type).linearity; }

bool is_linear_type(tree type) { return linearity_of(type) != Linearity::none; }

// Adds each linear field of the class TYPE to FIELDS: its own fields, and the
// fields of each base that is linear by its parts (the fields).  Returns false,
// which fails closed, when a base carries the annotation linear, when two
// paths of bases reach one field, or when the walk reaches its bound.
// Complexity: linear in the bases and the fields, and quadratic in the linear
// fields for the test of a second path.
bool collect_linear_fields(tree type, std::vector<tree>& fields, int depth) {
    if (depth > kTypeDepth) {
        return false;
    }
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            tree base = TYPE_MAIN_VARIANT(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)));
            Linearity base_linearity = linearity_of(base);
            if (base_linearity == Linearity::direct) {
                return false;
            }
            if (base_linearity == Linearity::derived && !collect_linear_fields(base, fields, depth + 1)) {
                return false;
            }
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) != FIELD_DECL || DECL_FIELD_IS_BASE(field) || TREE_TYPE(field) == NULL_TREE
            || TYPE_REF_P(TREE_TYPE(field)) || !is_linear_type(TREE_TYPE(field))) {
            continue;
        }
        if (std::find(fields.begin(), fields.end(), field) != fields.end()) {
            return false;
        }
        fields.push_back(field);
    }
    return true;
}

bool holds_linear_sum(tree type, int depth);

// Adds each field of the class TYPE that is or holds a linear sum to FIELDS,
// for a holder of a sum in the body of a door (the sum holders).  Returns
// false, which fails closed, when a base of TYPE holds a sum.  Complexity:
// linear in the bases and the fields, and the walk of each one.
bool collect_sum_fields(tree type, std::vector<tree>& fields) {
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            if (holds_linear_sum(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), 1)) {
                return false;
            }
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && !DECL_FIELD_IS_BASE(field) && TREE_TYPE(field) != NULL_TREE
            && !TYPE_REF_P(TREE_TYPE(field)) && holds_linear_sum(TREE_TYPE(field), 1)) {
            fields.push_back(field);
        }
    }
    return true;
}

// True when a temporary of TYPE drops a linear value at its end: TYPE is
// linear, and the end of its scope is no door.
bool is_dropped_temporary_type(tree type) {
    TypeFacts facts = facts_of(type);
    return facts.linearity != Linearity::none && !facts.ends_at_scope;
}

// True when TYPE is a linear class or a reference to one.
bool is_linear_or_reference(tree type) {
    if (type != NULL_TREE && TYPE_P(type) && TYPE_REF_P(type)) {
        type = TREE_TYPE(type);
    }
    return is_linear_type(type);
}

// True when TYPE is an rvalue reference to a linear class: the type of a move
// of a linear value.  A body with such a move and no linear object gets the
// walk too, because the move can name a place that no frame of the body owns
// (the moves through a borrow).
bool is_linear_move_type(tree type) {
    return type != NULL_TREE && TYPE_P(type) && TYPE_REF_P(type) && TYPE_REF_IS_RVALUE(type)
        && is_linear_type(TREE_TYPE(type));
}

// True when TYPE is a linear sum (the sums): a linear class that carries the
// annotation sum, such as an Option of a linear payload.  An array of sums is
// no sum.
bool is_linear_sum(tree type) {
    if (type == NULL_TREE || !TYPE_P(type)) {
        return false;
    }
    type = TYPE_MAIN_VARIANT(type);
    return CLASS_TYPE_P(type) && carries_annotation(TYPE_ATTRIBUTES(type), linear_names.sum) && is_linear_type(type);
}

// True when TYPE is a linear sum or holds one by value (the sum holders): the
// element of an array, a base, a data member that is no reference, and a
// type argument of a linear class, such as the payload of a Box or the
// element of a list, at each depth.  A pointer and a reference hold nothing.
// The walk reads each class, also one that cannot move.  Complexity: the walk
// visits each class one time.
bool holds_linear_sum(tree type, int depth = 0) {
    if (type == NULL_TREE || type == error_mark_node || depth > kTypeDepth || !TYPE_P(type)) {
        return false;
    }
    type = TYPE_MAIN_VARIANT(element_type_of(type));
    if (!CLASS_TYPE_P(type) || !COMPLETE_TYPE_P(type)) {
        return false;
    }
    auto found = linear_caches.holds_sum.find(type);
    if (found != linear_caches.holds_sum.end()) {
        return found->second;
    }
    // The answer while the walk is open, so that a cycle through a type
    // argument ends.
    linear_caches.holds_sum.emplace(type, false);
    bool const result = is_linear_sum(type) || parts_hold_linear_sum(type, is_linear_type(type), depth);
    linear_caches.holds_sum[type] = result;
    return result;
}

// True when a part of the complete class TYPE is or holds a linear sum: a
// base, a data member that is no reference, or a type argument when
// IS_LINEAR tells that TYPE is linear (holds_linear_sum).  A linear class is
// linear by the values that its type arguments name, so it holds them.
// Complexity: linear in the parts, and the walk of each part.
bool parts_hold_linear_sum(tree type, bool is_linear, int depth) {
    if (depth > kTypeDepth) {
        return false;
    }
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            if (holds_linear_sum(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), depth + 1)) {
                return true;
            }
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && !DECL_FIELD_IS_BASE(field) && TREE_TYPE(field) != NULL_TREE
            && !TYPE_REF_P(TREE_TYPE(field)) && holds_linear_sum(TREE_TYPE(field), depth + 1)) {
            return true;
        }
    }
    tree info = is_linear ? CLASSTYPE_TEMPLATE_INFO(type) : NULL_TREE;
    tree arguments = info != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)) : NULL_TREE;
    for (int index = 0; arguments != NULL_TREE && index < TREE_VEC_LENGTH(arguments); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (argument != NULL_TREE && TYPE_P(argument) && holds_linear_sum(argument, depth + 1)) {
            return true;
        }
    }
    return false;
}

// The summaries: the capture fields of each closure whose reference names a
// linear sum that the body of the call operator can fill, by the TYPE_UID of
// the closure, and the capture fields that each path of a call operator
// leaves empty, by the DECL_UID of the call operator.  Each function whose
// body keeps no reference parameter that names a linear sum, by its DECL_UID,
// and each function whose PLUGIN_PRE_GENERICIZE gave its fact.  A fact that
// waits for the facts of callees with a later body holds the DECL_UID of each
// such callee.  No collection reuses a UID, so the facts stay for the unit
// (the collections of plugin.h).
std::unordered_map<unsigned, std::vector<unsigned>> closure_fills;
std::unordered_map<unsigned, std::vector<unsigned>> closure_empties;
std::unordered_set<unsigned> keeps_no_sum_reference;
std::unordered_set<unsigned> returns_sum_reference;
std::unordered_set<unsigned> summarized_functions;
std::unordered_map<unsigned, std::vector<unsigned>> waiting_kept_facts;

// The closures that move a capture: each call operator whose body moves a
// linear capture out, by its DECL_UID.
std::unordered_set<unsigned> closure_moves_out;

// Closures that a call owns: each call operator whose body leaves a
// linear capture by value live on a path, and each call operator that the
// summary walk read, by its DECL_UID.
std::unordered_set<unsigned> closure_leaves_capture;
std::unordered_set<unsigned> closure_summarized;

// The same two facts by the TYPE_UID of the closure: a closure type whose
// call operator, or one instance of its call operator template, moves a linear
// capture out, or leaves a linear capture by value live on a path.  A generic
// lambda has a call operator template, and lambda_function gives that
// template, while the summary walk reads each instance (the generic closures).
std::unordered_set<unsigned> closure_types_move_out;
std::unordered_set<unsigned> closure_types_leave_capture;

// The named bodies: each class that is no closure, by its TYPE_UID, whose
// call operator && the summary walk read.  A named body that leaves a linear
// field live is in closure_types_leave_capture too.
std::unordered_set<unsigned> named_bodies_summarized;

// True when CALL calls the call operator of a closure that moves a linear
// capture out, with an implicit object parameter (the closures that move a
// capture).
bool calls_moving_closure(tree call) {
    if (call == NULL_TREE || TREE_CODE(call) != CALL_EXPR) {
        return false;
    }
    tree callee = cp_get_callee_fndecl_nofold(call);
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL || closure_moves_out.count(DECL_UID(callee)) == 0) {
        return false;
    }
    // A call operator with an explicit object parameter that is no rvalue
    // reference borrows its closure, as one with no qualifier && does.
    if (DECL_XOBJ_MEMBER_FUNCTION_P(callee)) {
        tree object = TREE_VALUE(TYPE_ARG_TYPES(TREE_TYPE(callee)));
        return !(TYPE_REF_P(object) && TYPE_REF_IS_RVALUE(object));
    }
    return TREE_CODE(TREE_TYPE(callee)) == METHOD_TYPE && !FUNCTION_RVALUE_QUALIFIED(TREE_TYPE(callee));
}

// True when TYPE, or the type that the reference TYPE names, is a closure
// whose call operator moves a linear capture out (the closures that move a
// capture).
bool is_moving_closure_type(tree type) {
    if (type != NULL_TREE && TYPE_REF_P(type)) {
        type = TREE_TYPE(type);
    }
    if (type == NULL_TREE || !LAMBDA_TYPE_P(type)) {
        return false;
    }
    type = TYPE_MAIN_VARIANT(type);
    tree call_operator = lambda_function(type);
    return (call_operator != NULL_TREE && closure_moves_out.count(DECL_UID(call_operator)) != 0)
        || closure_types_move_out.count(TYPE_UID(type)) != 0;
}

// The named bodies: true when FUNCTION is the call operator && of a class
// that is no closure and that holds a linear value, with an implicit object
// parameter.  The once door calls such a body one time as an rvalue, so the
// object of the call is the body that the door owns.
bool is_named_body_call(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || !DECL_IOBJ_MEMBER_FUNCTION_P(function)
        || !DECL_OVERLOADED_OPERATOR_IS(function, CALL_EXPR) || !FUNCTION_RVALUE_QUALIFIED(TREE_TYPE(function))
        || LAMBDA_FUNCTION_P(function)) {
        return false;
    }
    tree body = TYPE_MAIN_VARIANT(DECL_CONTEXT(function));
    return CLASS_TYPE_P(body) && !LAMBDA_TYPE_P(body) && linearity_of(body) != Linearity::none;
}

// True when CALL calls a closure that moves a linear capture out, or calls a
// function with the annotation once_door with such a closure, so the walk
// must read the object of the closure (the closures that move a capture).
// Complexity: linear in the arguments of the call.
bool calls_with_moving_closure(tree call) {
    if (calls_moving_closure(call)) {
        return true;
    }
    if (call == NULL_TREE || TREE_CODE(call) != CALL_EXPR) {
        return false;
    }
    tree callee = cp_get_callee_fndecl_nofold(call);
    if (callee == NULL_TREE || !carries_annotation(DECL_ATTRIBUTES(callee), linear_names.once_door)) {
        return false;
    }
    for (int index = 0; index < call_expr_nargs(call); ++index) {
        if (is_moving_closure_type(TREE_TYPE(CALL_EXPR_ARG(call, index)))) {
            return true;
        }
    }
    return false;
}

// True when NAME is an object of the frame of the function: a local that is
// no reference and no capture, or a parameter by value or by an rvalue
// reference, which the caller gave up.
bool is_frame_owned_name(tree name) {
    if (name == NULL_TREE || TREE_TYPE(name) == NULL_TREE) {
        return false;
    }
    bool is_lvalue_reference = TYPE_REF_P(TREE_TYPE(name)) && !TYPE_REF_IS_RVALUE(TREE_TYPE(name));
    if (TREE_CODE(name) == PARM_DECL) {
        return !is_lvalue_reference;
    }
    return VAR_P(name) && !TYPE_REF_P(TREE_TYPE(name)) && !DECL_HAS_VALUE_EXPR_P(name) && !TREE_STATIC(name)
        && !DECL_EXTERNAL(name);
}

// True when the object OBJECT of a call of a closure is a temporary, or an
// rvalue that std::move, std::forward or a cast to an rvalue reference gives
// of an object of the frame (is_frame_owned_name).  The front end writes a
// cast of a forwarding reference to its own type, such as
// static_cast<Body&&>(body), as a NON_LVALUE_EXPR of that type.  The full
// expression ends a temporary, so no second call reaches it, and the rule
// use-after-move refuses each later use of a name that such a cast spends.  A
// cast of a borrowed object, such as a parameter by an lvalue reference, a
// capture or the object behind a pointer, gives nothing up (the closures that
// move a capture).
bool is_object_given_up(tree object) {
    bool saw_rvalue = false;
    for (int depth = 0; object != NULL_TREE && depth < kTypeDepth; ++depth) {
        object = tree_strip_any_location_wrapper(object);
        switch (TREE_CODE(object)) {
            case ADDR_EXPR:
            case INDIRECT_REF:
                object = TREE_OPERAND(object, 0);
                continue;
            case NON_LVALUE_EXPR:
            case NOP_EXPR:
            case CONVERT_EXPR:
            case STATIC_CAST_EXPR:
            case VIEW_CONVERT_EXPR:
                saw_rvalue = saw_rvalue
                          || (TREE_TYPE(object) != NULL_TREE && TYPE_REF_P(TREE_TYPE(object))
                              && TYPE_REF_IS_RVALUE(TREE_TYPE(object)));
                object = TREE_OPERAND(object, 0);
                continue;
            case TARGET_EXPR:
            case CONSTRUCTOR:
                return true;
            case CALL_EXPR: {
                tree callee = cp_get_callee_fndecl_nofold(object);
                tree type = TREE_TYPE(object);
                if (!is_std_move_cast(callee) || call_expr_nargs(object) < 1) {
                    return false;
                }
                saw_rvalue = saw_rvalue || (type != NULL_TREE && TYPE_REF_P(type) && TYPE_REF_IS_RVALUE(type));
                object = CALL_EXPR_ARG(object, 0);
                continue;
            }
            case VAR_DECL:
            case PARM_DECL:
                return saw_rvalue && is_frame_owned_name(object);
            default:
                return false;
        }
    }
    return false;
}

// The summaries: what a callee does with a reference parameter that names a
// linear sum, at one point of the unit.
enum class KeptFact : std::uint8_t {
    keeps_nothing,  // the body keeps no such reference
    can_keep,  // the body can keep one, or no body exists for the call
    unknown,  // the unit genericizes the body after this point, or the fact waits for such a body
};

// The fact of the callee CALLEE at this point of the unit.  A call through a
// pointer can keep each reference.
KeptFact kept_fact_of(tree callee) {
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL) {
        return KeptFact::can_keep;
    }
    unsigned uid = DECL_UID(callee);
    if (keeps_no_sum_reference.count(uid) != 0) {
        return KeptFact::keeps_nothing;
    }
    if (summarized_functions.count(uid) != 0 && waiting_kept_facts.count(uid) == 0) {
        return KeptFact::can_keep;
    }
    return KeptFact::unknown;
}

// The fact that each callee that the walk of a body found with no fact keeps
// nothing, after the walks of the unit.
bool keeps_nothing_each(const std::vector<unsigned>& callees) {
    return std::all_of(callees.begin(), callees.end(),
                       [](unsigned uid) { return keeps_no_sum_reference.count(uid) != 0; });
}

// Gives each waiting fact whose callees keep nothing its fact, until no fact
// changes.  A fact that waits on itself, also through other callees, stays a
// fact that can keep, so recursion fails closed.  Complexity: quadratic in the
// waiting facts in the worst case, and each unit holds a few.
void resolve_waiting_kept_facts() {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto entry = waiting_kept_facts.begin(); entry != waiting_kept_facts.end();) {
            if (keeps_nothing_each(entry->second)) {
                keeps_no_sum_reference.insert(entry->first);
                entry = waiting_kept_facts.erase(entry);
                changed = true;
            } else {
                ++entry;
            }
        }
    }
}

// True when FIELD is a capture by reference of a linear sum.
bool is_sum_capture(tree field) {
    tree type = field != NULL_TREE ? TREE_TYPE(field) : NULL_TREE;
    return type != NULL_TREE && TYPE_REF_P(type) && !TYPE_READONLY(TREE_TYPE(type)) && is_linear_sum(TREE_TYPE(type));
}

// True when FIELD is a capture of a linear value that the body of the call
// operator can move out: a capture by copy, or a capture by a reference that
// is not const (the closures that move a capture).
bool is_linear_capture(tree field) {
    tree type = field != NULL_TREE && TREE_CODE(field) == FIELD_DECL ? TREE_TYPE(field) : NULL_TREE;
    if (type != NULL_TREE && TYPE_REF_P(type)) {
        type = TYPE_READONLY(TREE_TYPE(type)) ? NULL_TREE : TREE_TYPE(type);
    }
    return type != NULL_TREE && is_linear_type(type);
}

// True when FIELD is a capture by value of a linear value, which the closure
// owns (the closures that a call owns).
bool is_owned_linear_capture(tree field) {
    tree type = field != NULL_TREE && TREE_CODE(field) == FIELD_DECL ? TREE_TYPE(field) : NULL_TREE;
    return type != NULL_TREE && !TYPE_REF_P(type) && is_linear_type(type);
}

// True when a parameter of the type PARAMETER_TYPE lets the callee write its
// argument: a reference that is not const, or a pointer to a type that is not
// const.  The object parameter of a member that is not const is such a
// pointer.
bool is_writing_parameter(tree parameter_type) {
    if (parameter_type == NULL_TREE) {
        return false;
    }
    bool is_lvalue_reference = TYPE_REF_P(parameter_type) && !TYPE_REF_IS_RVALUE(parameter_type);
    return (is_lvalue_reference || TYPE_PTR_P(parameter_type)) && !TYPE_READONLY(TREE_TYPE(parameter_type));
}

// True when the function FUNCTION, or the template of an instance of it,
// carries the annotation NAME of include/foundation/Linear.h.
bool carries_linear_annotation(tree function, tree name) {
    return function_carries_base_annotation(function, name, linear_names.linear);
}

// True when CALL calls a move assignment of a class: an operator= whose
// parameter after the object is an rvalue reference to a class (the
// self-moves).  A session handle over a Resource that is no linear class is
// no linear class either, and its move assignment counts too.  A body with
// such a call is read, also when each object of the body is behind a
// reference or a pointer.
bool is_move_assignment_call(tree call) {
    if (call == NULL_TREE || TREE_CODE(call) != CALL_EXPR) {
        return false;
    }
    tree callee = cp_get_callee_fndecl_nofold(call);
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL || !DECL_ASSIGNMENT_OPERATOR_P(callee)) {
        return false;
    }
    tree parameters = TYPE_ARG_TYPES(TREE_TYPE(callee));
    tree source = parameters != NULL_TREE ? TREE_CHAIN(parameters) : NULL_TREE;
    tree source_type = source != NULL_TREE ? TREE_VALUE(source) : NULL_TREE;
    return source_type != NULL_TREE && TYPE_REF_P(source_type) && TYPE_REF_IS_RVALUE(source_type)
        && CLASS_TYPE_P(TREE_TYPE(source_type));
}

// True for an accessor: a member function with an implicit object parameter
// and no qualifier &&, that gives an lvalue reference to a linear value, such
// as operator* or value() of std::optional (the accessors).  A member function
// with the qualifier && consumes its object by itself.
bool is_accessor(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_LANG_SPECIFIC(function) == nullptr
        || !DECL_IOBJ_MEMBER_FUNCTION_P(function) || DECL_CONSTRUCTOR_P(function) || DECL_DESTRUCTOR_P(function)
        || DECL_ASSIGNMENT_OPERATOR_P(function) || FUNCTION_RVALUE_QUALIFIED(TREE_TYPE(function))) {
        return false;
    }
    tree result = TREE_TYPE(TREE_TYPE(function));
    return result != NULL_TREE && TYPE_REF_P(result) && !TYPE_REF_IS_RVALUE(result)
        && is_linear_type(TREE_TYPE(result));
}

// The reference helpers: a function that gives back one of its parameters by
// an lvalue reference, such as through(T& value) { return value; }.  A move
// of its result moves the object of that argument, so the walk goes through
// the call to the argument, as it goes through std::move.  The map holds the
// index of that parameter by the DECL_UID of each function whose body the
// rule read, or kNoParameter.  The index counts the object parameter of a
// member first, as the arguments of a call do.
constexpr int kNoParameter = -1;
std::unordered_map<int, int> forwarded_parameters;

// The parameter that NODE gives back through the casts, an address, a
// dereference and the store of the result, or null.
tree returned_parameter(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        switch (TREE_CODE(node)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case VIEW_CONVERT_EXPR:
            case STATIC_CAST_EXPR:
            case IMPLICIT_CONV_EXPR:
            case ADDR_EXPR:
            case INDIRECT_REF:
            case PAREN_EXPR:
                node = TREE_OPERAND(node, 0);
                continue;
            case INIT_EXPR:
            case MODIFY_EXPR:
                node = TREE_OPERAND(node, 1);
                continue;
            case PARM_DECL:
                return node;
            default:
                return NULL_TREE;
        }
    }
    return NULL_TREE;
}

// The state of the walk of the returns of one function for the reference
// helpers.
struct ParameterReturns {
    tree parameter = NULL_TREE;
    bool saw_return = false;
};

tree walk_parameter_returns(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    if (node == NULL_TREE || TYPE_P(node) || TREE_CODE(node) == LAMBDA_EXPR) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    if (TREE_CODE(node) != RETURN_EXPR) {
        return NULL_TREE;
    }
    auto& walk = *static_cast<ParameterReturns*>(data);
    tree parameter = returned_parameter(TREE_OPERAND(node, 0));
    walk.parameter = !walk.saw_return || walk.parameter == parameter ? parameter : NULL_TREE;
    walk.saw_return = true;
    return NULL_TREE;
}

// The index of the parameter by an lvalue reference that each return of
// FUNCTION gives back, or kNoParameter (the reference helpers).  The function
// gives an lvalue reference.  An instance of a template whose body the front
// end has not made yet reads the body of its pattern, whose parameters stand
// in the same places.  A function with no body in the unit, or with a body
// later in the unit, gives kNoParameter, and the walk then reads no move
// through it.  Complexity: linear in the body, one time for each function.
int forwarded_parameter_index(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return kNoParameter;
    }
    tree result = TREE_TYPE(TREE_TYPE(function));
    if (result == NULL_TREE || !TYPE_REF_P(result) || TYPE_REF_IS_RVALUE(result)) {
        return kNoParameter;
    }
    auto cached = forwarded_parameters.find(static_cast<int>(DECL_UID(function)));
    if (cached != forwarded_parameters.end()) {
        return cached->second;
    }
    tree source = function;
    if (DECL_SAVED_TREE(function) == NULL_TREE && DECL_LANG_SPECIFIC(function) != nullptr
        && DECL_TEMPLATE_INFO(function) != NULL_TREE && DECL_TEMPLATE_INSTANTIATION(function)) {
        tree pattern = DECL_TEMPLATE_RESULT(most_general_template(DECL_TI_TEMPLATE(function)));
        source = pattern != NULL_TREE && TREE_CODE(pattern) == FUNCTION_DECL ? pattern : function;
    }
    tree body = DECL_SAVED_TREE(source);
    if (body == NULL_TREE) {
        return kNoParameter;
    }
    ParameterReturns walk;
    cp_walk_tree_without_duplicates(&body, walk_parameter_returns, &walk);
    int index = kNoParameter;
    tree returned = walk.saw_return ? walk.parameter : NULL_TREE;
    if (returned != NULL_TREE && TYPE_REF_P(TREE_TYPE(returned)) && !TYPE_REF_IS_RVALUE(TREE_TYPE(returned))) {
        int position = 0;
        for (tree parameter = DECL_ARGUMENTS(source); parameter != NULL_TREE;
             parameter = DECL_CHAIN(parameter), ++position) {
            if (parameter == returned) {
                index = position;
                break;
            }
        }
    }
    forwarded_parameters.emplace(static_cast<int>(DECL_UID(function)), index);
    return index;
}

// The type of the function that a call names, or null.
tree function_type_of(tree call, tree callee) {
    if (callee != NULL_TREE) {
        return TREE_TYPE(callee);
    }
    if (TREE_CODE(call) != CALL_EXPR || CALL_EXPR_FN(call) == NULL_TREE) {
        return NULL_TREE;
    }
    tree type = TREE_TYPE(CALL_EXPR_FN(call));
    if (type != NULL_TREE && POINTER_TYPE_P(type)) {
        type = TREE_TYPE(type);
    }
    return type != NULL_TREE && FUNC_OR_METHOD_TYPE_P(type) ? type : NULL_TREE;
}

// The name of a function in an entity, with no template argument.  A
// constructor and a destructor have the name of their class.
std::string function_text(tree function) {
    tree scope = DECL_CONTEXT(function);
    bool is_special = DECL_CONSTRUCTOR_P(function) || DECL_DESTRUCTOR_P(function);
    if (!is_special || scope == NULL_TREE || !TYPE_P(scope) || TYPE_NAME(scope) == NULL_TREE) {
        return qualified_name(function);
    }
    std::string name = qualified_name(TYPE_NAME(scope));
    std::string last = simple_name(TYPE_NAME(scope));
    return name + (DECL_DESTRUCTOR_P(function) ? "::~" : "::") + last;
}

// ── The analysis of one function ────────────────────────────────────────

// The status of one tracked object on the paths that reach a point.
enum class Status : std::uint8_t {
    dead,  // not declared yet, or out of scope
    live,  // each path holds the value
    consumed,  // each path consumed it
    maybe,  // some paths consumed it
};

// The content of one tracked sum on the paths that reach a point (the sums).
// A sum is empty only when each path shows it empty, and full only when each
// path shows it full.  Each other sum can hold a payload, so the default is
// can_be_full.
enum class Content : std::uint8_t {
    can_be_full,
    empty,
    full,
};

struct FlowState {
    bool is_reachable = true;
    std::vector<Status> status;
    std::vector<Content> content;

    [[nodiscard]] Status get(std::size_t slot) const { return slot < status.size() ? status[slot] : Status::dead; }
    void set(std::size_t slot, Status value) {
        if (slot >= status.size()) {
            status.resize(slot + 1, Status::dead);
        }
        status[slot] = value;
    }
    [[nodiscard]] Content get_content(std::size_t slot) const {
        return slot < content.size() ? content[slot] : Content::can_be_full;
    }
    void set_content(std::size_t slot, Content value) {
        if (slot >= content.size()) {
            content.resize(slot + 1, Content::can_be_full);
        }
        content[slot] = value;
    }
};

FlowState unreachable_state() {
    FlowState state_of_no_path;
    state_of_no_path.is_reachable = false;
    return state_of_no_path;
}

// The join of two states at a point that two paths reach.  The join is
// monotone, so a fixed point of a loop stays above each state of its passes.
// A slot that one path holds dead takes the status and the content of the
// other path.  Complexity: linear in the slots.
FlowState join(const FlowState& left, const FlowState& right) {
    if (!left.is_reachable) {
        return right;
    }
    if (!right.is_reachable) {
        return left;
    }
    FlowState joined;
    std::size_t size = std::max({left.status.size(), right.status.size(), left.content.size(), right.content.size()});
    joined.status.resize(size, Status::dead);
    joined.content.resize(size, Content::can_be_full);
    for (std::size_t slot = 0; slot < size; ++slot) {
        Status one = left.get(slot);
        Status other = right.get(slot);
        if (one == other || other == Status::dead) {
            joined.status[slot] = one;
        } else if (one == Status::dead) {
            joined.status[slot] = other;
        } else {
            joined.status[slot] = Status::maybe;
        }
        Content one_content = left.get_content(slot);
        Content other_content = right.get_content(slot);
        if (other == Status::dead) {
            joined.content[slot] = one_content;
        } else if (one == Status::dead) {
            joined.content[slot] = other_content;
        } else {
            joined.content[slot] = one_content == other_content ? one_content : Content::can_be_full;
        }
    }
    return joined;
}

bool is_same_state(const FlowState& left, const FlowState& right) {
    if (left.is_reachable != right.is_reachable) {
        return false;
    }
    std::size_t size = std::max({left.status.size(), right.status.size(), left.content.size(), right.content.size()});
    // The content of a dead slot means nothing, so the compare skips it.
    for (std::size_t slot = 0; slot < size; ++slot) {
        Status status = left.get(slot);
        if (status != right.get(slot)
            || (status != Status::dead && left.get_content(slot) != right.get_content(slot))) {
            return false;
        }
    }
    return true;
}

// The bound of the slots of one object: the object and its fields (the
// fields).
constexpr std::size_t kNoParent = SIZE_MAX;

// The end of a value in the body of a door (the doors): exempt from the kinds
// of a drop, or kept, because the value is or holds a linear sum, which no
// door ends (the sum holders).
enum class DoorEnd : std::uint8_t {
    exempt,
    kept,
};

struct Slot {
    tree decl = NULL_TREE;
    // The field of a slot of one field of the object DECL, or null for the
    // slot of the whole object (the fields).
    tree field = NULL_TREE;
    std::size_t parent = kNoParent;
    std::vector<std::size_t> fields;
    // The end of the scope of the object is a door (the linear types).
    bool ends_at_scope = false;
    // The object is a linear sum, whose end reads its content (the sums).
    bool is_sum = false;
    // The end of the object in the body of a door: kept when the object is or
    // holds a linear sum (the sum holders).
    DoorEnd door_end = DoorEnd::exempt;
    // A capture by reference, a stored address or a reference member names
    // the sum, so a later write through it can fill the sum (the sums).
    bool is_aliased = false;
    // A call took the sum by a reference or a pointer that is not const, and
    // the callee can keep it and fill the sum at a later point, so no fact
    // of the content holds for the known content after it (the sums).
    bool is_borrowed = false;
    // A stored address, a reference member, a callee that can keep a
    // reference or a closure that a callee can keep names the sum, so each
    // call that starts after its consume can fill it again (the aliases).
    bool can_refill_at_calls = false;
    // A closure captured the object by reference, so the rule cannot see its
    // consume, and the rule linear-capture reports the capture (the captures).
    bool is_escaped = false;
    // The summaries: the object is a capture of a linear value in the body of
    // a call operator, which the call borrows from the closure.  Its end
    // gives no finding.  LEAVES_PAYLOAD tells that a path to an exit can
    // leave a payload in a sum, and LEAVES_MOVED_OUT that a path to an exit
    // can leave the capture consumed (the closures that move a capture).
    // LEAVES_LIVE tells that a path to an exit can leave the capture or one
    // of its fields live (the closures that a call owns).
    bool is_capture_borrow = false;
    bool leaves_payload = false;
    bool leaves_moved_out = false;
    bool leaves_live = false;
    unsigned capture_field_uid = 0;
    // The lent parameters: the object is a parameter that the caller lends,
    // the location of its first consume, and whether a path reaches an exit
    // with the object or one of its fields consumed.
    bool is_lent = false;
    // The named bodies: the object of the call operator && of a class that is
    // no closure, in the summary walk.  LEAVES_LIVE tells that a path to an
    // exit can leave the object or one of its linear fields live.
    bool is_owned_body = false;
    location_t first_consume = UNKNOWN_LOCATION;
    bool has_spent_exit = false;
    bool has_live_exit = false;
    bool has_maybe_exit = false;
    bool was_double_reported = false;
    bool was_assign_reported = false;
};

// The target of a break or a continue: a loop or a switch.
struct Target {
    std::size_t scope_depth = 0;
    bool is_loop = false;
    std::vector<FlowState> breaks;
    std::vector<FlowState> continues;
};

struct SwitchContext {
    FlowState entry;
    bool has_default = false;
};

struct PendingFinding {
    Kind kind;
    location_t location;
    std::string entity;
    DoorEnd door_end = DoorEnd::exempt;
    // The TYPE_UID of a closure that a once door owns and whose call operator
    // had no fact at the walk, or 0.  Such a line waits for the end of the
    // unit (the generic closures).
    unsigned waits_for_closure_type = 0;
    // The line stays when no summary read the call operator by the end of the
    // unit (the named bodies).
    bool fails_closed = false;
};

// One finding of a walk of a body, with its place, before the rule adds it.
// It holds no tree, so it can wait for the end of the unit (the summaries).
struct WalkFinding {
    Kind kind = Kind::linear_unconsumed;
    Place place;
    std::string entity;

    [[nodiscard]] bool operator==(const WalkFinding& other) const {
        return kind == other.kind && place.file == other.place.file && place.line == other.place.line
            && place.column == other.place.column && entity == other.entity;
    }
};

// The findings of a body that depend on callees with no fact at its walk:
// the findings of the walk where such a callee can keep a reference, the
// findings of the walk where it keeps none, and the callees.  The end of the
// unit adds one of the two lists (the summaries).
struct DeferredFindings {
    std::vector<WalkFinding> if_kept;
    std::vector<WalkFinding> if_not_kept;
    std::vector<unsigned> callees;
};

std::vector<DeferredFindings> deferred_findings;

// A line of the kind capture-unconsumed that waits for the end of the unit:
// the once door owns a closure whose call operator had no fact at the walk
// (the generic closures).  A named body with no summary at the end keeps its
// line (the named bodies).
struct WaitingOwnedClosure {
    unsigned closure_type = 0;
    bool fails_closed = false;
    WalkFinding finding;
};

std::vector<WaitingOwnedClosure> waiting_owned_closures;

// How a walk of a body reads a callee with no fact (the summaries).
enum class UnknownCallee : std::uint8_t {
    can_keep,
    keeps_nothing,
};

// What a walk of a body gives: the findings of the rule, or only the
// summary of a call operator (the summaries).
enum class WalkPurpose : std::uint8_t {
    findings,
    summary,
};

// The bound of the passes of the fixed point of one loop.  The lattice of one
// object has the height 3, and the content of a sum adds 1, so the head of a
// loop stops changing after a few passes.  A loop that reaches the bound keeps
// the state of its last pass.
constexpr int kLoopPasses = 6;

// The state of the walk of a body for the dropped elements: each node of the
// body and of the originals of its folds, and each statement that can own the
// move of a dropped element.
struct PresenceWalk {
    hash_set<tree> visited;
    std::vector<tree> statements;
};

tree walk_presence(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    if (node == NULL_TREE || TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    auto& walk = *static_cast<PresenceWalk*>(data);
    tree_code code = TREE_CODE(node);
    if ((code == DECL_EXPR || code == EXPR_STMT || code == RETURN_EXPR) && EXPR_HAS_LOCATION(node)) {
        walk.statements.push_back(node);
    }
    auto found = folded_initializers.find(node);
    if (found != folded_initializers.end()) {
        for (const FoldEntry& entry : found->second) {
            tree original = entry.original;
            cp_walk_tree(&original, walk_presence, data, &walk.visited);
        }
    }
    return NULL_TREE;
}

// How the walk to a tracked object reads a base of the object with no field
// after it (the fields, the base doors).
enum class BaseStep : std::uint8_t {
    no_slot,  // a base is no slot, so the walk names no object
    whole_object,  // the walk names the whole object, for the object of a member with the qualifier &&
};

// The branch of a condition that a path takes (the sums, the tests).
enum class TestBranch : std::uint8_t {
    condition_true,
    condition_false,
};

// True when the special member FUNCTION moves or transfers a linear value by
// its contract: a move constructor or a move assignment, or a constructor or
// an assignment of a class that carries the annotation linear (the objects).
bool is_transfer_function(tree function) {
    if (DECL_LANG_SPECIFIC(function) != nullptr && move_fn_p(function)) {
        return true;
    }
    tree scope = DECL_CONTEXT(function);
    bool is_special = DECL_CONSTRUCTOR_P(function) || DECL_ASSIGNMENT_OPERATOR_P(function);
    return is_special && scope != NULL_TREE && TYPE_P(scope) && linearity_of(scope) == Linearity::direct;
}

// True when FUNCTION is a member of a union that a base file declares, such as
// the alternatives of a sum (the shapes).  The union ends one alternative of
// each type, and its owner owes that end, so a line of drop-door there names
// its place and no type.
bool is_base_union_member(tree function) {
    tree scope = function != NULL_TREE ? DECL_CONTEXT(function) : NULL_TREE;
    if (scope == NULL_TREE || TREE_CODE(scope) != UNION_TYPE) {
        return false;
    }
    return place_of(DECL_SOURCE_LOCATION(function), Scope::quarantine).file_class == FileClass::base;
}

// True when FUNCTION carries the annotation moves_own_storage and a base file
// declares it (the moves through a borrow).  A function outside the base can
// carry the annotation, but the rule does not trust it there.
bool moves_own_storage(tree function) {
    if (!carries_linear_annotation(function, linear_names.moves_own_storage)) {
        return false;
    }
    return place_of(DECL_SOURCE_LOCATION(function), Scope::quarantine).file_class == FileClass::base;
}

tree capture_field_of(tree node, tree closure);
void collect_brands(tree type, std::vector<tree>& out, int depth);

// The kinds: the text of the linear type of an object or a temporary in the
// entity of a line.  type_text of library.cpp gives one text for one type in
// each unit, with each template argument.  A fresh brand names one call site,
// so each brand of collect_brands becomes the word "brand".  Two instances of
// one template that end two payloads at one line then give two lines, and two
// brands of one payload give one line.  Complexity: linear in the nodes of the
// type, and in the length of its text for each brand.
std::string linear_type_text(tree type) {
    if (type == NULL_TREE || type == error_mark_node || !TYPE_P(type)) {
        return "(type)";
    }
    if (TYPE_REF_P(type)) {
        type = TREE_TYPE(type);
    }
    type = TYPE_MAIN_VARIANT(type);
    std::string text = type_text(type);
    const std::string word = "brand";
    std::vector<tree> brands;
    collect_brands(type, brands, 0);
    for (tree brand : brands) {
        std::string spelled = type_text(brand);
        if (spelled.empty() || spelled == word) {
            continue;
        }
        for (std::size_t at = text.find(spelled); at != std::string::npos; at = text.find(spelled, at + word.size())) {
            text.replace(at, spelled.size(), word);
        }
    }
    return text;
}

class Analysis {
public:
    Analysis(tree function, bool is_door, UnknownCallee unknown_callee, WalkPurpose purpose)
        : function_(function), is_door_(is_door), unknown_callee_(unknown_callee), purpose_(purpose) {}

    // The walk of the body.  A second walk of one body takes the dropped
    // elements of the first walk, because the first walk takes the folds of
    // the body from the list of the unit (the dropped elements).
    void run(const Analysis* first_walk = nullptr) {
        if (first_walk != nullptr) {
            orphans_ = first_walk->orphans_;
        } else {
            collect_orphans();
        }
        scopes_.emplace_back();
        FlowState entry;
        if (!is_exempt_function()) {
            for (tree parameter = DECL_ARGUMENTS(function_); parameter != NULL_TREE;
                 parameter = DECL_CHAIN(parameter)) {
                std::size_t slot = 0;
                if (track(parameter, slot)) {
                    scopes_.back().push_back(slot);
                    set_object(entry, slot, Status::live);
                }
            }
        }
        walk_statement(DECL_SAVED_TREE(function_), entry);
        if (entry.is_reachable) {
            exit_scopes(entry, 0, true);
        }
        report();
    }

    [[nodiscard]] const std::vector<WalkFinding>& findings() const { return findings_; }

    // The callees with no fact that the walk read (the summaries).
    [[nodiscard]] const std::vector<unsigned>& unknown_callees() const { return unknown_callees_; }

    // The summaries: the capture fields of a call operator that each path to
    // an exit leaves empty.  A walk with a goto gives none.
    [[nodiscard]] std::vector<unsigned> emptied_capture_fields() const {
        std::vector<unsigned> fields;
        if (is_unanalyzed_) {
            return fields;
        }
        for (const Slot& entry : slots_) {
            if (entry.is_capture_borrow && entry.is_sum && !entry.leaves_payload) {
                fields.push_back(entry.capture_field_uid);
            }
        }
        return fields;
    }

    // The closures that move a capture: true when a path to an exit of the
    // call operator leaves a linear capture consumed, or when the body gives
    // a capture that is such a closure to the once door.  A walk with a goto
    // fails closed for each linear capture (is_unanalyzed).
    [[nodiscard]] bool moves_capture_out() const {
        if (is_unanalyzed_) {
            return std::any_of(slots_.begin(), slots_.end(), [](const Slot& entry) { return entry.is_capture_borrow; });
        }
        return gives_capture_to_once_door_ || std::any_of(slots_.begin(), slots_.end(), [](const Slot& entry) {
                   return entry.is_capture_borrow && entry.leaves_moved_out;
               });
    }

    // Closures that a call owns: the capture fields of a call operator
    // that each path to an exit ends: it consumes them, or leaves an empty
    // sum.  A walk with a goto gives none.
    [[nodiscard]] std::vector<unsigned> ended_capture_fields() const {
        std::vector<unsigned> fields;
        if (is_unanalyzed_) {
            return fields;
        }
        for (const Slot& entry : slots_) {
            if (entry.is_capture_borrow && !entry.leaves_live) {
                fields.push_back(entry.capture_field_uid);
            }
        }
        return fields;
    }

    // The named bodies: true when a path to an exit of the call operator &&
    // leaves the object or one of its linear fields live.  A walk with a goto
    // fails closed.
    [[nodiscard]] bool leaves_owned_body_live() const {
        return is_unanalyzed_ || std::any_of(slots_.begin(), slots_.end(), [](const Slot& entry) {
                   return entry.is_owned_body && entry.leaves_live;
               });
    }

    // True when the body has a goto or a label, so the walk did not read it.
    [[nodiscard]] bool is_unanalyzed() const { return is_unanalyzed_; }

private:
    tree function_;
    bool is_door_;
    UnknownCallee unknown_callee_;
    WalkPurpose purpose_;
    std::vector<WalkFinding> findings_;
    std::vector<unsigned> unknown_callees_;
    std::vector<Slot> slots_;
    std::unordered_map<tree, std::size_t> slot_of_;
    // Each lvalue reference local that borrows a tracked object, and that
    // object.  A move through the reference consumes the object.
    std::unordered_map<tree, tree> referent_of_;
    // The summaries: each capture field of a closure of this body that names a
    // tracked sum by reference, with that sum, by the TYPE_UID of the closure.
    std::unordered_map<unsigned, std::vector<std::pair<unsigned, tree>>> capture_targets_;
    // Each lvalue reference local, and the object that its initializer names
    // (self_move_place), also when the rule tracks no object of it (the
    // self-moves).
    std::unordered_map<tree, tree> alias_place_of_;
    // Each local lvalue reference and its initializer (the moves through a
    // borrow).
    std::unordered_map<tree, tree> reference_initial_;
    std::vector<std::vector<std::size_t>> scopes_;
    std::vector<Target*> targets_;
    std::vector<SwitchContext*> switches_;
    std::vector<PendingFinding> pending_;
    // The originals of the folds that the walk reads at this time.  A folded
    // tree can be a subtree of its original, so the walk does not read an
    // original inside itself.
    std::vector<tree> open_originals_;
    // The sources of the empty-class assignments that the walk reads at this
    // time (the folds), so that the walk of a source does not read its
    // assignment again.
    std::vector<tree> open_assignments_;
    // The location of the statement after the one that the walk reads, or
    // UNKNOWN_LOCATION at the end of the body.
    location_t next_statement_ = UNKNOWN_LOCATION;
    bool is_unanalyzed_ = false;
    // The closures that move a capture: the body gives a capture of its
    // closure that moves a linear capture out to the once door.
    bool gives_capture_to_once_door_ = false;
    // The dropped elements: the original of each dropped element of this
    // body, and the statement that owns its move.
    struct Orphan {
        tree original = NULL_TREE;
        tree statement = NULL_TREE;
        location_t location = UNKNOWN_LOCATION;
    };
    std::vector<Orphan> orphans_;

    bool is_exempt_function() const { return is_transfer_function(function_); }

    // The dropped elements: each fold of an element that moves a local or a
    // parameter of this function, whose folded tree the body and the originals
    // of its folds do not hold, with the statement that owns the move.  The
    // walk takes the folds of this function from the list.  Complexity: linear
    // in the body, and in the folds times the statements.
    void collect_orphans() {
        auto is_of_function = [this](const ElementFold& fold) { return DECL_CONTEXT(fold.moved) == function_; };
        std::vector<ElementFold> folds;
        for (const ElementFold& fold : element_folds) {
            if (is_of_function(fold)) {
                folds.push_back(fold);
            }
        }
        if (folds.empty()) {
            return;
        }
        // The walk of a summary leaves the folds to the walk of the findings.
        if (purpose_ == WalkPurpose::findings) {
            element_folds.erase(std::remove_if(element_folds.begin(), element_folds.end(), is_of_function),
                                element_folds.end());
        }
        PresenceWalk walk;
        tree body = DECL_SAVED_TREE(function_);
        cp_walk_tree(&body, walk_presence, &walk, &walk.visited);
        for (const ElementFold& fold : folds) {
            if (walk.visited.contains(fold.folded)) {
                continue;
            }
            location_t where = expansion_point(fold.location);
            tree owner = NULL_TREE;
            location_t owner_start = UNKNOWN_LOCATION;
            for (tree statement : walk.statements) {
                location_t start = expansion_point(EXPR_LOCATION(statement));
                bool is_at_or_before = linemap_compare_locations(line_table, start, where) >= 0;
                if (is_at_or_before
                    && (owner == NULL_TREE || linemap_compare_locations(line_table, owner_start, start) >= 0)) {
                    owner = statement;
                    owner_start = start;
                }
            }
            if (owner != NULL_TREE) {
                orphans_.push_back(Orphan{fold.original, owner, fold.location});
            }
        }
    }

    // The moves of the dropped elements that the statement STATEMENT owns.
    void resolve_orphans(tree statement, FlowState& state) {
        for (const Orphan& orphan : orphans_) {
            if (orphan.statement != statement) {
                continue;
            }
            bool saw_rvalue = false;
            tree field = NULL_TREE;
            tree source = strip_to_tracked(orphan.original, saw_rvalue, &field);
            if (source != NULL_TREE && saw_rvalue) {
                consume(source, field, state, orphan.location);
            }
        }
    }

    // Gives DECL a slot when it is a tracked object (the objects).
    bool track(tree decl, std::size_t& slot) {
        auto found = slot_of_.find(decl);
        if (found != slot_of_.end()) {
            slot = found->second;
            return true;
        }
        tree type = TREE_TYPE(decl);
        if (type == NULL_TREE || type == error_mark_node) {
            return false;
        }
        bool is_lent = false;
        bool is_owned_body = false;
        if (VAR_P(decl)) {
            bool is_binding = DECL_DECOMPOSITION_P(decl) && !DECL_DECOMP_IS_BASE(decl);
            if (purpose_ == WalkPurpose::summary && DECL_HAS_VALUE_EXPR_P(decl) && !is_binding) {
                return track_capture_borrow(decl, slot);
            }
            // The objects: a reference that extends the life of a temporary,
            // also the hidden range of a range-based loop, names the object.
            bool is_extending = binds_extended_temporary(decl, DECL_INITIAL(decl));
            if (TREE_STATIC(decl) || DECL_EXTERNAL(decl) || DECL_THREAD_LOCAL_P(decl)
                || (DECL_HAS_VALUE_EXPR_P(decl) && !is_binding)
                || (DECL_ARTIFICIAL(decl) && !is_binding && !is_extending)) {
                return false;
            }
            if (TYPE_REF_P(type)) {
                if (!is_binding && !is_extending) {
                    return false;
                }
                type = TREE_TYPE(type);
            }
        } else if (TREE_CODE(decl) == PARM_DECL && is_lent_this(decl)) {
            // The lent object: the object of a plain member function is lent,
            // as a parameter by a reference that is not const is.
            is_lent = true;
            type = TREE_TYPE(type);
        } else if (TREE_CODE(decl) == PARM_DECL && is_owned_body_this(decl)) {
            // The named bodies: the once door owns the object of the call
            // operator && of a named body, as it owns a closure.
            is_owned_body = true;
            type = TREE_TYPE(type);
        } else if (TREE_CODE(decl) == PARM_DECL) {
            if (DECL_ARTIFICIAL(decl) || is_object_parameter(decl)) {
                return false;
            }
            if (TYPE_REF_P(type)) {
                // The lent parameters: a reference that is not const lends
                // the object, and a const reference only reads it.
                is_lent = !TYPE_REF_IS_RVALUE(type);
                if (is_lent && TYPE_READONLY(TREE_TYPE(type))) {
                    return false;
                }
                type = TREE_TYPE(type);
            }
        } else {
            return false;
        }
        TypeFacts facts = facts_of(type);
        if (facts.linearity == Linearity::none) {
            return false;
        }
        slot = slots_.size();
        Slot entry;
        entry.decl = decl;
        entry.ends_at_scope = facts.ends_at_scope;
        entry.is_lent = is_lent;
        entry.is_owned_body = is_owned_body;
        entry.is_sum = is_linear_sum(type);
        entry.door_end = door_end_of(type);
        slots_.push_back(entry);
        slot_of_.emplace(decl, slot);
        add_field_slots(slot, type);
        return true;
    }

    // The objects: true when the local reference DECL extends the life of a
    // temporary: its initializer INITIAL gives the address of a temporary, or
    // of the hidden variable that the front end makes for such a temporary.
    // A reference whose initializer calls a function gets INITIAL in an
    // initialization after its declaration.  The base of a structured binding
    // is no such reference.
    static bool binds_extended_temporary(tree decl, tree initial) {
        if (!VAR_P(decl) || !TYPE_REF_P(TREE_TYPE(decl)) || DECL_DECOMPOSITION_P(decl) || TREE_STATIC(decl)
            || DECL_EXTERNAL(decl) || DECL_HAS_VALUE_EXPR_P(decl)) {
            return false;
        }
        tree node = initial;
        bool saw_address = false;
        for (int depth = 0; node != NULL_TREE && node != error_mark_node && depth < kTypeDepth; ++depth) {
            node = tree_strip_any_location_wrapper(node);
            switch (TREE_CODE(node)) {
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case CLEANUP_POINT_EXPR:
                    node = TREE_OPERAND(node, 0);
                    continue;
                case COMPOUND_EXPR:
                    node = TREE_OPERAND(node, 1);
                    continue;
                case ADDR_EXPR:
                    saw_address = true;
                    node = TREE_OPERAND(node, 0);
                    continue;
                case TARGET_EXPR:
                    return true;
                case VAR_DECL:
                    return saw_address && DECL_ARTIFICIAL(node) && !TREE_STATIC(node) && !DECL_DECOMPOSITION_P(node)
                        && !DECL_HAS_VALUE_EXPR_P(node);
                default:
                    return false;
            }
        }
        return false;
    }

    // The objects: a slot for the reference DECL that an initialization after
    // its declaration binds to a linear temporary, in the innermost scope,
    // which holds the declaration.
    void track_extending(tree decl) {
        tree type = TREE_TYPE(TREE_TYPE(decl));
        TypeFacts facts = facts_of(type);
        if (facts.linearity == Linearity::none) {
            return;
        }
        std::size_t slot = slots_.size();
        Slot entry;
        entry.decl = decl;
        entry.ends_at_scope = facts.ends_at_scope;
        entry.is_sum = is_linear_sum(type);
        entry.door_end = door_end_of(type);
        slots_.push_back(entry);
        slot_of_.emplace(decl, slot);
        add_field_slots(slot, type);
        scopes_.back().push_back(slot);
    }

    // The summaries: the proxy DECL of a capture by reference of a linear sum
    // in the body of a call operator gets a slot that the closure borrows.
    bool track_capture_borrow(tree decl, std::size_t& slot) {
        tree closure = DECL_CONTEXT(function_);
        if (closure == NULL_TREE || !LAMBDA_TYPE_P(closure)) {
            return false;
        }
        tree field = capture_field_of(DECL_VALUE_EXPR(decl), TYPE_MAIN_VARIANT(closure));
        if (!is_linear_capture(field)) {
            return false;
        }
        tree type = TREE_TYPE(field);
        type = TYPE_REF_P(type) ? TREE_TYPE(type) : type;
        slot = slots_.size();
        Slot entry;
        entry.decl = decl;
        entry.is_sum = is_linear_sum(type);
        entry.is_capture_borrow = true;
        entry.capture_field_uid = DECL_UID(field);
        slots_.push_back(entry);
        slot_of_.emplace(decl, slot);
        add_field_slots(slot, type);
        return true;
    }

    // The fields: an object of a class that is linear by its parts gets one
    // slot for each linear field, also of each base that is linear by its
    // parts.  A move of a field consumes its slot, and the move of the last
    // linear field consumes the object.  A class that carries the annotation
    // hides its fields.  Complexity: linear in the bases and the fields.
    //
    // In the body of a door, the end of a holder of a linear sum ends each
    // other value that the holder holds (the sum holders).  So such a holder
    // gets one slot for each of its own fields that is or holds a linear sum,
    // also when its class carries the annotation, and the consume of each such
    // field consumes the holder.  A base that holds a sum gives no slot, so
    // only a consume of the whole holder ends it.
    void add_field_slots(std::size_t slot, tree type) {
        type = TYPE_MAIN_VARIANT(type);
        bool const is_kept_holder = slots_[slot].door_end == DoorEnd::kept && !slots_[slot].is_sum;
        if (!CLASS_TYPE_P(type) || (linearity_of(type) != Linearity::derived && !is_kept_holder)) {
            return;
        }
        std::vector<tree> linear_fields;
        if (is_kept_holder ? !collect_sum_fields(type, linear_fields)
                           : !collect_linear_fields(type, linear_fields, 0)) {
            return;
        }
        std::vector<std::size_t> fields;
        for (tree field : linear_fields) {
            Slot part;
            part.decl = slots_[slot].decl;
            part.field = field;
            part.parent = slot;
            fields.push_back(slots_.size());
            slots_.push_back(part);
        }
        slots_[slot].fields = std::move(fields);
    }

    // The slot of the field FIELD of the tracked object DECL, or the slot of
    // DECL when FIELD is null.  Returns kNoParent when the object has no slot
    // for the field.
    std::size_t slot_for(tree decl, tree field) const {
        std::size_t slot = slot_of_.at(decl);
        if (field == NULL_TREE) {
            return slot;
        }
        for (std::size_t part : slots_[slot].fields) {
            if (slots_[part].field == field) {
                return part;
            }
        }
        return kNoParent;
    }

    // Sets the object of SLOT and each of its fields to STATUS.  A new value
    // can hold a payload, so a live sum can be full until a fact shows it
    // empty (the sums).
    void set_object(FlowState& state, std::size_t slot, Status status) {
        state.set(slot, status);
        if (status == Status::live) {
            state.set_content(slot, Content::can_be_full);
        }
        for (std::size_t part : slots_[slot].fields) {
            state.set(part, status);
        }
    }

    // The slot of the tracked sum that DECL and FIELD name, or kNoParent for
    // a field or an object that is no sum (the sums).
    std::size_t sum_slot(tree decl, tree field) const {
        if (decl == NULL_TREE || field != NULL_TREE) {
            return kNoParent;
        }
        std::size_t slot = slot_of_.at(decl);
        return slots_[slot].is_sum ? slot : kNoParent;
    }

    // A use of a tracked sum that can fill it, such as a reference that is not
    // const or an address (the sums).
    void note_can_be_full(tree decl, tree field, FlowState& state) {
        std::size_t slot = sum_slot(decl, field);
        if (slot != kNoParent && state.is_reachable) {
            state.set_content(slot, Content::can_be_full);
        }
    }

    // A capture by reference, a stored address that is not const or a
    // reference member that is not const names a tracked sum (the sums).  A
    // write through it can fill the sum at each later point, so the sum stays
    // one that can be full until a consume, and no test and no take shows it
    // empty after it.
    void note_alias(tree decl, tree field, FlowState& state) {
        std::size_t slot = sum_slot(decl, field);
        if (slot == kNoParent) {
            return;
        }
        slots_[slot].is_aliased = true;
        note_can_be_full(decl, field, state);
    }

    // The aliases: an alias that is no capture of a closure of this body, such
    // as a stored address, a reference member or a reference that a callee
    // can keep, can fill the tracked sum at each call after its consume.
    void note_kept_alias(tree decl, tree field, FlowState& state) {
        std::size_t slot = sum_slot(decl, field);
        if (slot == kNoParent) {
            return;
        }
        note_alias(decl, field, state);
        slots_[slot].can_refill_at_calls = true;
    }

    // True when a fact of a test, of take, of fill or of a builder can show
    // the content of the tracked sum of SLOT: the sum has no alias (the sums).
    bool can_show_content(std::size_t slot) const { return slot != kNoParent && !slots_[slot].is_aliased; }

    // The content of the sum that VALUE, the initializer of a tracked sum,
    // builds, through the temporaries and the casts of the initializer (the
    // sums): empty for a constructor that carries the annotation empties, and
    // full for a constructor or a static member that carries fills, such as
    // some.  The caller gives the initializer before its fold.  Each other
    // value can be full.
    static Content built_content(tree value) {
        for (int depth = 0; value != NULL_TREE && depth < kTypeDepth; ++depth) {
            value = tree_strip_any_location_wrapper(value);
            switch (TREE_CODE(value)) {
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case CLEANUP_POINT_EXPR:
                    value = TREE_OPERAND(value, 0);
                    continue;
                case TARGET_EXPR:
                    value = TARGET_EXPR_INITIAL(value);
                    continue;
                case CALL_EXPR:
                case AGGR_INIT_EXPR: {
                    tree callee = cp_get_callee_fndecl_nofold(value);
                    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL) {
                        return Content::can_be_full;
                    }
                    bool is_builder = DECL_CONSTRUCTOR_P(callee) || DECL_STATIC_FUNCTION_P(callee);
                    if (DECL_CONSTRUCTOR_P(callee) && carries_linear_annotation(callee, linear_names.empties)) {
                        return Content::empty;
                    }
                    if (is_builder && carries_linear_annotation(callee, linear_names.fills)) {
                        return Content::full;
                    }
                    return Content::can_be_full;
                }
                default:
                    return Content::can_be_full;
            }
        }
        return Content::can_be_full;
    }

    // The content of the tracked object DECL, which VALUE initializes, when
    // DECL is a sum and VALUE builds an empty or a full sum (the sums).
    void note_initial_content(tree decl, tree value, FlowState& state) {
        std::size_t slot = sum_slot(decl, NULL_TREE);
        if (can_show_content(slot) && state.is_reachable) {
            state.set_content(slot, built_content(value));
        }
    }

    // The known content: a member of a tracked sum that carries needs_empty,
    // such as fill or expect_none, on a sum that each path shows full, and a
    // member that carries needs_full, such as expect, on a sum that each path
    // shows empty.  The run-time check of such a member ends the process on
    // each path, so the rule refuses the call at compile time.  A sum that a
    // call borrowed gives no refusal, because the callee can keep the
    // reference and change the sum at each later call.
    void check_known_content(tree decl, tree field, tree callee, location_t location, const FlowState& state) {
        std::size_t slot = sum_slot(decl, field);
        if (!can_show_content(slot) || slots_[slot].is_borrowed || callee == NULL_TREE || !state.is_reachable) {
            return;
        }
        Content content = state.get_content(slot);
        if (content == Content::full && carries_linear_annotation(callee, linear_names.needs_empty)) {
            add_pending(Kind::linear_option_known_full, location,
                        entity_of(decl) + " " + identifier_text(DECL_NAME(callee)));
        } else if (content == Content::empty && carries_linear_annotation(callee, linear_names.needs_full)) {
            add_pending(Kind::linear_option_known_empty, location,
                        entity_of(decl) + " " + identifier_text(DECL_NAME(callee)));
        }
    }

    // The tests: the branch BRANCH of the condition CONDITION finds a tracked
    // sum empty or full when the condition is a test of its state, such as
    // is_none() of an Option.  The walk goes through the casts and through !.
    void refine_by_test(tree condition, TestBranch branch, FlowState& state) const {
        if (!state.is_reachable) {
            return;
        }
        bool is_true = branch == TestBranch::condition_true;
        for (int depth = 0; condition != NULL_TREE && depth < kTypeDepth; ++depth) {
            condition = tree_strip_any_location_wrapper(condition);
            switch (TREE_CODE(condition)) {
                case TRUTH_NOT_EXPR:
                    is_true = !is_true;
                    condition = TREE_OPERAND(condition, 0);
                    continue;
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case CLEANUP_POINT_EXPR:
                case SAVE_EXPR:
                    condition = TREE_OPERAND(condition, 0);
                    continue;
                case CALL_EXPR:
                    break;
                default:
                    return;
            }
            tree callee = cp_get_callee_fndecl_nofold(condition);
            if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL || call_expr_nargs(condition) < 1) {
                return;
            }
            bool is_empty_test = carries_linear_annotation(callee, linear_names.tests_empty);
            bool is_full_test = !is_empty_test && carries_linear_annotation(callee, linear_names.tests_full);
            if (!is_empty_test && !is_full_test) {
                return;
            }
            bool says_empty = is_empty_test == is_true;
            bool saw_rvalue = false;
            tree field = NULL_TREE;
            tree decl = strip_to_tracked(CALL_EXPR_ARG(condition, 0), saw_rvalue, &field);
            std::size_t slot = sum_slot(decl, field);
            if (can_show_content(slot)) {
                state.set_content(slot, says_empty ? Content::empty : Content::full);
            }
            return;
        }
    }

    // A call of a member function of a tracked sum (the sums): a member that
    // carries the annotation empties, such as take, leaves the sum empty, and
    // a member that carries fills, such as fill, gives it a new payload to
    // consume.  Each other member that can change the sum, with an object
    // parameter that is not const, can fill it.  A const member reads it.
    void note_sum_member(tree decl, tree field, tree type, tree callee, FlowState& state) {
        std::size_t slot = sum_slot(decl, field);
        if (slot == kNoParent || !state.is_reachable) {
            return;
        }
        if (callee != NULL_TREE && carries_linear_annotation(callee, linear_names.empties)) {
            if (can_show_content(slot)) {
                state.set_content(slot, Content::empty);
            }
            return;
        }
        if (callee != NULL_TREE && carries_linear_annotation(callee, linear_names.fills)) {
            set_object(state, slot, Status::live);
            if (can_show_content(slot)) {
                state.set_content(slot, Content::full);
            }
            return;
        }
        tree object_type = TYPE_ARG_TYPES(type) != NULL_TREE ? TREE_VALUE(TYPE_ARG_TYPES(type)) : NULL_TREE;
        bool is_const_object =
            object_type != NULL_TREE && INDIRECT_TYPE_P(object_type) && TYPE_READONLY(TREE_TYPE(object_type));
        if (!is_const_object) {
            state.set_content(slot, Content::can_be_full);
        }
    }

    void mark_escaped(tree decl) {
        std::size_t slot = slot_of_.at(decl);
        slots_[slot].is_escaped = true;
        for (std::size_t part : slots_[slot].fields) {
            slots_[part].is_escaped = true;
        }
    }

    // True when PARAMETER is the implicit this of a member function that
    // borrows its object (the lent object): no constructor, no destructor, no
    // call operator of a closure, no qualifier &&, and an object that is not
    // const.  A move of a linear member out of such an object is a move out
    // of a borrow, and a second call would give a moved-from member.
    bool is_lent_this(tree parameter) const {
        if (parameter != DECL_ARGUMENTS(function_) || !DECL_IOBJ_MEMBER_FUNCTION_P(function_)
            || DECL_CONSTRUCTOR_P(function_) || DECL_DESTRUCTOR_P(function_) || LAMBDA_FUNCTION_P(function_)
            || FUNCTION_RVALUE_QUALIFIED(TREE_TYPE(function_))) {
            return false;
        }
        tree type = TREE_TYPE(parameter);
        return type != NULL_TREE && TREE_CODE(type) == POINTER_TYPE && !TYPE_READONLY(TREE_TYPE(type));
    }

    // True when PARAMETER is the implicit this of the call operator && of a
    // named body in the summary walk (the named bodies): a class that is no
    // closure, whose object the once door owns and calls one time.
    bool is_owned_body_this(tree parameter) const {
        return purpose_ == WalkPurpose::summary && parameter == DECL_ARGUMENTS(function_)
            && is_named_body_call(function_);
    }

    // True when PARAMETER is the object parameter of the function: the
    // implicit this, or the explicit object parameter.
    bool is_object_parameter(tree parameter) const {
        if (parameter != DECL_ARGUMENTS(function_)) {
            return false;
        }
        return DECL_IOBJ_MEMBER_FUNCTION_P(function_) || DECL_XOBJ_MEMBER_FUNCTION_P(function_);
    }

    // The initializer before the fold of NODE, or NODE (the folds).  OBJECT is
    // the object that NODE initializes, or null.  An entry of that object comes
    // first.
    tree unfolded(tree node, tree object) const {
        if (node == NULL_TREE) {
            return node;
        }
        auto found = folded_initializers.find(node);
        if (found == folded_initializers.end() || found->second.empty()) {
            return node;
        }
        tree original = found->second.front().original;
        for (const FoldEntry& entry : found->second) {
            if (object != NULL_TREE && entry.object == object) {
                original = entry.original;
                break;
            }
        }
        if (std::find(open_originals_.begin(), open_originals_.end(), original) != open_originals_.end()) {
            return node;
        }
        return original;
    }

    // The slot field of the tracked object DECL whose DECL_UID is FIELD_UID, or
    // null when the object has no slot for it (the accessors).
    tree field_by_uid(tree decl, int field_uid) const {
        if (field_uid == kWholeObject) {
            return NULL_TREE;
        }
        for (std::size_t part : slots_[slot_of_.at(decl)].fields) {
            if (static_cast<int>(DECL_UID(slots_[part].field)) == field_uid) {
                return slots_[part].field;
            }
        }
        return NULL_TREE;
    }

    // The tracked object that NODE names through the casts of the consumes, or
    // null.  SAW_RVALUE is true when a cast to an rvalue reference or std::move
    // is on the way.  With FIELD, the walk also goes through a member access
    // to a linear field of the object (the fields), and FIELD gets that field,
    // or null for the whole object.  After a move cast, it goes through an
    // accessor: a member function that gives a reference to a linear value of
    // its object, such as operator* or value() of std::optional.  The move of
    // that reference consumes the field that the accessor gives, or the object
    // (the accessors).  BASE_STEP tells what a base of the object with no field
    // after it names (the base doors).
    tree strip_to_tracked(tree node, bool& saw_rvalue, tree* field = nullptr,
                          BaseStep base_step = BaseStep::no_slot) const {
        if (field != nullptr) {
            *field = NULL_TREE;
        }
        // The innermost field that is no base on the way, and whether a base
        // of the object comes with no such field after it.
        tree leaf = NULL_TREE;
        bool is_base_only = false;
        // The DECL_UID of the field that the last accessor on the way gives.
        int accessor_field = kWholeObject;
        for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
            node = unfolded(node, NULL_TREE);
            switch (TREE_CODE(node)) {
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case VIEW_CONVERT_EXPR:
                case IMPLICIT_CONV_EXPR:
                case STATIC_CAST_EXPR:
                    if (TREE_TYPE(node) != NULL_TREE && TYPE_REF_P(TREE_TYPE(node))
                        && TYPE_REF_IS_RVALUE(TREE_TYPE(node))) {
                        saw_rvalue = true;
                    }
                    node = TREE_OPERAND(node, 0);
                    continue;
                case ADDR_EXPR:
                case INDIRECT_REF:
                case CLEANUP_POINT_EXPR:
                case SAVE_EXPR:
                case PAREN_EXPR:
                    node = TREE_OPERAND(node, 0);
                    continue;
                case COMPOUND_EXPR: {
                    // An argument of an empty class by value is the argument
                    // and an EMPTY_CLASS_EXPR after it.
                    bool is_empty_argument =
                        TREE_CODE(tree_strip_any_location_wrapper(TREE_OPERAND(node, 1))) == EMPTY_CLASS_EXPR;
                    node = TREE_OPERAND(node, is_empty_argument ? 0 : 1);
                    continue;
                }
                case TARGET_EXPR:
                    node = TARGET_EXPR_INITIAL(node);
                    continue;
                case COMPONENT_REF: {
                    tree member = TREE_OPERAND(node, 1);
                    if (field == nullptr || TREE_CODE(member) != FIELD_DECL) {
                        return NULL_TREE;
                    }
                    if (!DECL_FIELD_IS_BASE(member)) {
                        leaf = member;
                        is_base_only = false;
                    } else if (leaf == NULL_TREE) {
                        is_base_only = true;
                    }
                    accessor_field = kWholeObject;
                    node = TREE_OPERAND(node, 0);
                    continue;
                }
                case CALL_EXPR: {
                    tree callee = cp_get_callee_fndecl_nofold(node);
                    if (is_std_move_cast(callee) && call_expr_nargs(node) >= 1) {
                        saw_rvalue = saw_rvalue || DECL_NAME(callee) != linear_names.forward;
                        node = CALL_EXPR_ARG(node, 0);
                        continue;
                    }
                    // A reference helper names the object of one argument
                    // (the reference helpers).
                    int const forwarded = forwarded_parameter_index(callee);
                    if (forwarded != kNoParameter && forwarded < call_expr_nargs(node)) {
                        node = CALL_EXPR_ARG(node, forwarded);
                        continue;
                    }
                    if (saw_rvalue && is_accessor(callee) && call_expr_nargs(node) >= 1) {
                        auto summary = accessor_fields.find(static_cast<int>(DECL_UID(callee)));
                        accessor_field = summary != accessor_fields.end() ? summary->second : kWholeObject;
                        leaf = NULL_TREE;
                        is_base_only = false;
                        node = CALL_EXPR_ARG(node, 0);
                        continue;
                    }
                    return NULL_TREE;
                }
                case VAR_DECL:
                case PARM_DECL: {
                    tree found = node;
                    if (slot_of_.count(node) == 0) {
                        auto referent = referent_of_.find(node);
                        found = referent != referent_of_.end() ? referent->second : NULL_TREE;
                    }
                    if (found == NULL_TREE || field == nullptr) {
                        return found;
                    }
                    // A base of the object with no field after it is no
                    // field slot, and neither is a field with no slot.  The
                    // object of a member with the qualifier && names the
                    // whole object through its base (the base doors).
                    if (is_base_only) {
                        return base_step == BaseStep::whole_object ? found : NULL_TREE;
                    }
                    if (leaf != NULL_TREE && slot_for(found, leaf) == kNoParent) {
                        return NULL_TREE;
                    }
                    *field = leaf != NULL_TREE ? leaf : field_by_uid(found, accessor_field);
                    return found;
                }
                default:
                    return NULL_TREE;
            }
        }
        return NULL_TREE;
    }

    // The type of NODE when NODE is a linear temporary through the casts,
    // whose end is no door, or null.
    static tree linear_temporary_type(tree node) {
        for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
            switch (TREE_CODE(node)) {
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case VIEW_CONVERT_EXPR:
                case ADDR_EXPR:
                case CLEANUP_POINT_EXPR:
                    node = TREE_OPERAND(node, 0);
                    continue;
                case TARGET_EXPR:
                    return is_dropped_temporary_type(TREE_TYPE(node)) ? TREE_TYPE(node) : NULL_TREE;
                default:
                    return NULL_TREE;
            }
        }
        return NULL_TREE;
    }

    // The object that NODE names, for the test of a self-move (the self-moves):
    // NODE through the casts, the addresses, the dereferences, std::move and
    // std::forward, and a local lvalue reference to the object of its
    // initializer.
    tree self_move_place(tree node) const {
        for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
            node = tree_strip_any_location_wrapper(node);
            switch (TREE_CODE(node)) {
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case VIEW_CONVERT_EXPR:
                case IMPLICIT_CONV_EXPR:
                case STATIC_CAST_EXPR:
                case ADDR_EXPR:
                case INDIRECT_REF:
                case PAREN_EXPR:
                    node = TREE_OPERAND(node, 0);
                    continue;
                case CALL_EXPR: {
                    tree callee = cp_get_callee_fndecl_nofold(node);
                    if (is_std_move_cast(callee) && call_expr_nargs(node) >= 1) {
                        node = CALL_EXPR_ARG(node, 0);
                        continue;
                    }
                    return node;
                }
                case VAR_DECL: {
                    auto referent = referent_of_.find(node);
                    if (referent != referent_of_.end()) {
                        return referent->second;
                    }
                    auto alias = alias_place_of_.find(node);
                    return alias != alias_place_of_.end() ? alias->second : node;
                }
                default:
                    return node;
            }
        }
        return node;
    }

    // True when TARGET and SOURCE name one object (the self-moves).  A side
    // with a side effect names no object, so two calls or two increments that
    // look alike are no self-move.
    bool is_self_move(tree target, tree source) const {
        tree target_place = self_move_place(target);
        tree source_place = self_move_place(source);
        if (target_place == NULL_TREE || source_place == NULL_TREE || target_place == error_mark_node
            || source_place == error_mark_node || TREE_SIDE_EFFECTS(target_place) || TREE_SIDE_EFFECTS(source_place)) {
            return false;
        }
        return target_place == source_place || cp_tree_equal(target_place, source_place);
    }

    // FUNCTION: OBJECT (TYPE) for a self-move of TARGET.  The object is the
    // field of a member, the variable of an element or of a local, or
    // "object".  TARGET is the object of the assignment, or its address for a
    // call of operator=.
    std::string self_move_entity(tree target) const {
        tree place = self_move_place(target);
        while (place != NULL_TREE && TREE_CODE(place) == ARRAY_REF) {
            place = tree_strip_any_location_wrapper(TREE_OPERAND(place, 0));
        }
        tree named = place != NULL_TREE && TREE_CODE(place) == COMPONENT_REF ? TREE_OPERAND(place, 1) : place;
        std::string name = named != NULL_TREE && DECL_P(named) ? simple_name(named) : std::string{"object"};
        tree object_type = target != NULL_TREE ? TREE_TYPE(target) : NULL_TREE;
        if (object_type != NULL_TREE && POINTER_TYPE_P(object_type)) {
            object_type = TREE_TYPE(object_type);
        }
        return function_text(function_) + ": " + name + " (" + linear_type_text(object_type) + ")";
    }

    // FUNCTION: OBJECT (TYPE) of the tracked object DECL (the kinds).
    std::string entity_of(tree decl) const {
        return function_text(function_) + ": " + simple_name(decl) + " (" + linear_type_text(TREE_TYPE(decl)) + ")";
    }

    void add_pending(Kind kind, location_t location, const std::string& entity, DoorEnd door_end = DoorEnd::exempt) {
        pending_.push_back(PendingFinding{kind, location, entity, door_end});
    }

    // The end of a value of TYPE in the body of this function (the sum
    // holders): kept in the body of a door when TYPE is or holds a linear sum.
    DoorEnd door_end_of(tree type) const {
        return is_door_ && holds_linear_sum(type) ? DoorEnd::kept : DoorEnd::exempt;
    }

    static bool is_used(Status status) { return status == Status::consumed || status == Status::maybe; }

    // A consume of the object DECL, or of its field FIELD (the fields).  A
    // consume of the whole after a consume of a field, or of a field after a
    // consume of the whole, is a second consume.
    void consume(tree decl, tree field, FlowState& state, location_t location) {
        if (!state.is_reachable) {
            return;
        }
        std::size_t slot = slot_for(decl, field);
        if (slot == kNoParent) {
            return;
        }
        std::size_t whole = slot_of_.at(decl);
        Status status = state.get(slot);
        bool was_used = is_used(status);
        if (field == NULL_TREE) {
            for (std::size_t part : slots_[whole].fields) {
                was_used = was_used || is_used(state.get(part));
            }
        } else {
            was_used = was_used || is_used(state.get(whole));
        }
        if (was_used && !slots_[whole].was_double_reported && !slots_[whole].is_escaped) {
            slots_[whole].was_double_reported = true;
            add_pending(Kind::linear_double_consume, location, entity_of(decl));
        }
        if (status == Status::dead) {
            return;
        }
        if (slots_[whole].is_lent && slots_[whole].first_consume == UNKNOWN_LOCATION) {
            slots_[whole].first_consume = location;
        }
        if (field == NULL_TREE) {
            // A consumer takes the payload out of a sum, so the sum is empty
            // after it (the sums).
            set_object(state, whole, Status::consumed);
            state.set_content(whole, Content::empty);
            return;
        }
        state.set(slot, Status::consumed);
        bool is_each_field_consumed = true;
        for (std::size_t part : slots_[whole].fields) {
            is_each_field_consumed = is_each_field_consumed && state.get(part) == Status::consumed;
        }
        if (is_each_field_consumed) {
            state.set(whole, Status::consumed);
        }
    }

    // The lent parameters: an assignment to the field FIELD of the lent
    // object DECL gives the field a value again.  The object is live again
    // when each path consumed it, and each other consumed field stays
    // consumed.  An object that the rule does not lend keeps its states,
    // because the rule drop-door reads an assignment to a field.
    void refill_lent_field(tree decl, tree field, FlowState& state) {
        std::size_t whole = slot_of_.at(decl);
        std::size_t slot = slot_for(decl, field);
        if (!slots_[whole].is_lent || slot == kNoParent || !state.is_reachable) {
            return;
        }
        state.set(slot, Status::live);
        if (state.get(whole) == Status::consumed) {
            state.set(whole, Status::live);
        }
    }

    void assign_over(tree decl, FlowState& state, location_t location) {
        if (!state.is_reachable) {
            return;
        }
        std::size_t slot = slot_of_.at(decl);
        Status status = state.get(slot);
        if ((status == Status::live || status == Status::maybe) && !slots_[slot].was_assign_reported
            && !slots_[slot].is_escaped) {
            slots_[slot].was_assign_reported = true;
            add_pending(Kind::linear_assign_over_live, location, entity_of(decl));
        }
        set_object(state, slot, Status::live);
    }

    // True when a live object drops a value at an exit on each path: it has no
    // field slot, or a field that each path holds (the fields).  An object
    // whose fields are consumed on some paths only drops a value on a path.
    bool is_live_at_exit(const FlowState& state, std::size_t slot) const {
        if (slots_[slot].fields.empty()) {
            return true;
        }
        for (std::size_t part : slots_[slot].fields) {
            if (state.get(part) == Status::live) {
                return true;
            }
        }
        return false;
    }

    // True when a path can reach this point with the object of SLOT or one of
    // its fields consumed (the lent parameters).
    bool is_spent(const FlowState& state, std::size_t slot) const {
        if (is_used(state.get(slot))) {
            return true;
        }
        return std::any_of(slots_[slot].fields.begin(), slots_[slot].fields.end(),
                           [&state](std::size_t part) { return is_used(state.get(part)); });
    }

    // Notes the status of each object of the scopes from DEPTH on at an exit.
    // KILL makes them dead in STATE.
    void exit_scopes(FlowState& state, std::size_t depth, bool kill) {
        if (!state.is_reachable) {
            return;
        }
        for (std::size_t scope = depth; scope < scopes_.size(); ++scope) {
            for (std::size_t slot : scopes_[scope]) {
                Status status = state.get(slot);
                const Slot& entry = slots_[slot];
                if (entry.is_lent) {
                    // The lent parameters: the caller keeps the object, so its
                    // end owes no consume, and a consumed object or field is a
                    // spent value of the caller.
                    slots_[slot].has_spent_exit = entry.has_spent_exit || (!entry.is_escaped && is_spent(state, slot));
                } else if (entry.is_owned_body && !entry.ends_at_scope) {
                    // The named bodies: the once door owns the object, so a
                    // linear field that a path leaves live, or the object of a
                    // class that is linear by its own mark, drops with it.
                    bool is_live_here = entry.fields.empty() && status != Status::consumed;
                    for (std::size_t part : entry.fields) {
                        is_live_here = is_live_here || state.get(part) != Status::consumed;
                    }
                    slots_[slot].leaves_live = slots_[slot].leaves_live || is_live_here;
                } else if (entry.is_capture_borrow) {
                    // The summaries: the call borrows the capture, so its end
                    // gives no finding.  A slot that the walk never made live
                    // fails closed for the two facts.
                    bool is_empty_here = status == Status::consumed || state.get_content(slot) == Content::empty;
                    bool is_kept_here = status == Status::live;
                    for (std::size_t part : entry.fields) {
                        is_kept_here = is_kept_here && state.get(part) == Status::live;
                    }
                    slots_[slot].leaves_payload = slots_[slot].leaves_payload || !is_empty_here;
                    slots_[slot].leaves_moved_out = slots_[slot].leaves_moved_out || !is_kept_here;
                    slots_[slot].leaves_live = slots_[slot].leaves_live || !is_empty_here;
                } else if (entry.ends_at_scope || (entry.is_escaped && !entry.is_sum)) {
                    // The end of the scope is the door of the object, or a
                    // capture by reference hides the consume (the captures).
                } else if (entry.is_sum && state.get_content(slot) == Content::empty) {
                    // An empty sum holds no payload, so its end drops none
                    // (the sums).
                } else if (status == Status::live && is_live_at_exit(state, slot)) {
                    slots_[slot].has_live_exit = true;
                } else if (status == Status::live || status == Status::maybe) {
                    slots_[slot].has_maybe_exit = true;
                }
                if (kill) {
                    set_object(state, slot, Status::dead);
                }
            }
        }
    }

    location_t location_or(tree node, location_t fallback) const {
        return node != NULL_TREE && EXPR_P(node) && EXPR_HAS_LOCATION(node) ? EXPR_LOCATION(node) : fallback;
    }

    location_t function_location() const { return DECL_SOURCE_LOCATION(function_); }

    // ── Statements ──

    void walk_statement(tree node, FlowState& state) {
        if (node == NULL_TREE || node == error_mark_node) {
            return;
        }
        switch (TREE_CODE(node)) {
            case STATEMENT_LIST: {
                location_t outer_next = next_statement_;
                for (tree_stmt_iterator item = tsi_start(node); !tsi_end_p(item); tsi_next(&item)) {
                    location_t next = outer_next;
                    tree_stmt_iterator ahead = item;
                    for (tsi_next(&ahead); !tsi_end_p(ahead); tsi_next(&ahead)) {
                        tree statement = tsi_stmt(ahead);
                        if (statement != NULL_TREE && EXPR_P(statement) && EXPR_HAS_LOCATION(statement)) {
                            next = EXPR_LOCATION(statement);
                            break;
                        }
                    }
                    next_statement_ = next;
                    walk_statement(tsi_stmt(item), state);
                }
                next_statement_ = outer_next;
                return;
            }
            case BIND_EXPR:
                scopes_.emplace_back();
                for (tree variable = BIND_EXPR_VARS(node); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
                    std::size_t slot = 0;
                    if (track(variable, slot)) {
                        scopes_.back().push_back(slot);
                    }
                }
                walk_statement(BIND_EXPR_BODY(node), state);
                exit_scopes(state, scopes_.size() - 1, true);
                scopes_.pop_back();
                return;
            case DECL_EXPR:
                walk_declaration(DECL_EXPR_DECL(node), state);
                resolve_orphans(node, state);
                return;
            case CLEANUP_STMT:
                // The cleanup runs at each exit of the body.  It is no consume.
                walk_statement(CLEANUP_BODY(node), state);
                return;
            case IF_STMT: {
                walk_statement(IF_COND(node), state);
                tree condition = strip_conversions(IF_COND(node));
                if (IF_STMT_CONSTEXPR_P(node) && condition != NULL_TREE && TREE_CODE(condition) == INTEGER_CST) {
                    walk_statement(integer_zerop(condition) ? ELSE_CLAUSE(node) : THEN_CLAUSE(node), state);
                    return;
                }
                FlowState then_state = state;
                FlowState else_state = state;
                refine_by_test(IF_COND(node), TestBranch::condition_true, then_state);
                refine_by_test(IF_COND(node), TestBranch::condition_false, else_state);
                walk_statement(THEN_CLAUSE(node), then_state);
                walk_statement(ELSE_CLAUSE(node), else_state);
                state = join(then_state, else_state);
                return;
            }
            case WHILE_STMT:
                walk_loop(WHILE_COND(node), WHILE_BODY(node), NULL_TREE, false, false, state);
                return;
            case FOR_STMT:
                walk_statement(FOR_INIT_STMT(node), state);
                walk_loop(FOR_COND(node), FOR_BODY(node), FOR_EXPR(node), false, false, state);
                return;
            case DO_STMT:
                walk_loop(DO_COND(node), DO_BODY(node), NULL_TREE, true, false, state);
                return;
            case RANGE_FOR_STMT:
                walk_expression(RANGE_FOR_EXPR(node), state);
                walk_loop(boolean_true_node, RANGE_FOR_BODY(node), NULL_TREE, false, true, state);
                return;
            case SWITCH_STMT:
                walk_switch(node, state);
                return;
            case CASE_LABEL_EXPR:
                if (!switches_.empty()) {
                    state = join(state, switches_.back()->entry);
                    if (CASE_LOW(node) == NULL_TREE) {
                        switches_.back()->has_default = true;
                    }
                }
                return;
            case BREAK_STMT:
                if (!targets_.empty() && state.is_reachable) {
                    Target* target = targets_.back();
                    FlowState broken = state;
                    exit_scopes(broken, target->scope_depth, true);
                    target->breaks.push_back(broken);
                }
                state.is_reachable = false;
                return;
            case CONTINUE_STMT:
                for (auto target = targets_.rbegin(); target != targets_.rend(); ++target) {
                    if ((*target)->is_loop) {
                        if (state.is_reachable) {
                            FlowState continued = state;
                            exit_scopes(continued, (*target)->scope_depth, true);
                            (*target)->continues.push_back(continued);
                        }
                        break;
                    }
                }
                state.is_reachable = false;
                return;
            case RETURN_EXPR:
                walk_expression(TREE_OPERAND(node, 0), state);
                resolve_orphans(node, state);
                exit_scopes(state, 0, false);
                state.is_reachable = false;
                return;
            case GOTO_EXPR:
            case LABEL_EXPR:
                is_unanalyzed_ = true;
                return;
            case TRY_BLOCK:
                walk_try(node, state);
                return;
            case EH_SPEC_BLOCK:
                walk_statement(EH_SPEC_STMTS(node), state);
                return;
            case USING_STMT:
                return;
            case EXPR_STMT:
                if (is_void_constant(EXPR_STMT_EXPR(node))) {
                    walk_removed_assignments(node, state);
                } else {
                    check_discarded(EXPR_STMT_EXPR(node));
                    walk_expression(EXPR_STMT_EXPR(node), state);
                }
                resolve_orphans(node, state);
                return;
            default:
                break;
        }
        if (STATEMENT_CLASS_P(node)) {
            // A statement that the rule does not name: each operand in order.
            int length = TREE_OPERAND_LENGTH(node);
            for (int index = 0; index < length; ++index) {
                walk_statement(TREE_OPERAND(node, index), state);
            }
            return;
        }
        check_discarded(node);
        walk_expression(node, state);
    }

    // A try block: a handler starts from the state before the block.
    void walk_try(tree node, FlowState& state) {
        FlowState entry = state;
        walk_statement(TRY_STMTS(node), state);
        auto walk_handler = [&](tree handler) {
            if (handler != NULL_TREE && TREE_CODE(handler) == HANDLER) {
                FlowState caught = entry;
                walk_statement(HANDLER_BODY(handler), caught);
                state = join(state, caught);
            }
        };
        tree handlers = TRY_HANDLERS(node);
        if (handlers != NULL_TREE && TREE_CODE(handlers) == STATEMENT_LIST) {
            for (tree_stmt_iterator item = tsi_start(handlers); !tsi_end_p(item); tsi_next(&item)) {
                walk_handler(tsi_stmt(item));
            }
        } else {
            walk_handler(handlers);
        }
    }

    void walk_declaration(tree decl, FlowState& state) {
        if (decl == NULL_TREE || !VAR_P(decl) || TREE_STATIC(decl) || DECL_EXTERNAL(decl)) {
            return;
        }
        tree initial = DECL_INITIAL(decl);
        bool lends_bindings = false;
        if (initial != NULL_TREE && initial != error_mark_node) {
            initial = unfolded(initial, decl);
            open_originals_.push_back(initial);
            walk_expression(initial, state);
            bool saw_rvalue = false;
            tree field = NULL_TREE;
            tree source = strip_to_tracked(initial, saw_rvalue, &field);
            open_originals_.pop_back();
            // An lvalue reference local borrows its object, and a move through
            // it consumes the object.  The base of a structured binding by
            // reference gives the object to its bindings.
            tree decl_type = TREE_TYPE(decl);
            bool is_borrow = decl_type != NULL_TREE && TYPE_REF_P(decl_type) && !TYPE_REF_IS_RVALUE(decl_type)
                          && !(DECL_DECOMPOSITION_P(decl) && DECL_DECOMP_IS_BASE(decl));
            if (is_borrow) {
                reference_initial_[decl] = initial;
            }
            // A reference to a field, also the base of a structured binding,
            // borrows the field and consumes nothing (the fields).
            bool is_field_borrow =
                field != NULL_TREE && decl_type != NULL_TREE && TYPE_REF_P(decl_type) && !TYPE_REF_IS_RVALUE(decl_type);
            // An address of a tracked sum in a pointer that is not const can
            // fill the sum (the sums).
            bool is_mutable_address =
                decl_type != NULL_TREE && TYPE_PTR_P(decl_type) && !TYPE_READONLY(TREE_TYPE(decl_type));
            // A reference to a plain local or parameter names that object for
            // the life of the reference.  A reference through a dereference or
            // an index names the object of the pointer or the index of that
            // time, and a later write of either can change the object that the
            // same expression names, so it names no alias (the self-moves).
            if (is_borrow) {
                tree place = self_move_place(initial);
                bool is_plain_object = place != NULL_TREE && place != decl
                                    && (TREE_CODE(place) == VAR_DECL || TREE_CODE(place) == PARM_DECL)
                                    && TREE_TYPE(place) != NULL_TREE && !POINTER_TYPE_P(TREE_TYPE(place));
                if (is_plain_object) {
                    alias_place_of_[decl] = place;
                }
            }
            note_given_back_alias(decl, initial, state);
            // The lent parameters: the base of a structured binding by
            // reference of a lent object borrows it, so each binding is a lent
            // part of the object of the caller.
            lends_bindings = source != NULL_TREE && DECL_DECOMPOSITION_P(decl) && DECL_DECOMP_IS_BASE(decl)
                          && field == NULL_TREE && is_lent_borrow(source, saw_rvalue, decl_type);
            if (source != NULL_TREE && source != decl && is_borrow && field == NULL_TREE) {
                referent_of_[decl] = source;
            } else if (lends_bindings) {
                // No consume: the bindings take the lent object below.
            } else if (source != NULL_TREE && source != decl && is_mutable_address) {
                note_kept_alias(source, field, state);
            } else if (source != NULL_TREE && source != decl && !is_borrow && !is_field_borrow
                       && (saw_rvalue || is_linear_or_reference(decl_type))) {
                consume(source, field, state, location_or(initial, DECL_SOURCE_LOCATION(decl)));
            } else if (source == NULL_TREE && decl_type != NULL_TREE && !TYPE_REF_P(decl_type)
                       && !TYPE_PTR_P(decl_type)) {
                check_borrow_route(initial, location_or(initial, DECL_SOURCE_LOCATION(decl)), state);
            }
        }
        std::size_t slot = 0;
        bool is_tracked = slot_of_.count(decl) != 0;
        if (!is_tracked && track(decl, slot)) {
            // A tracked object with no BIND_EXPR joins the innermost scope.
            scopes_.back().push_back(slot);
            is_tracked = true;
        }
        if (is_tracked && state.is_reachable) {
            set_object(state, slot_of_.at(decl), Status::live);
            // INITIAL is the initializer before its fold (the folds).
            if (initial != NULL_TREE && initial != error_mark_node) {
                note_initial_content(decl, initial, state);
            }
        }
        // The bindings of a structured binding, which have no DECL_EXPR of
        // their own.
        if (DECL_DECOMPOSITION_P(decl) && DECL_DECOMP_IS_BASE(decl) && state.is_reachable) {
            for (const auto& [tracked, tracked_slot] : slot_of_) {
                if (VAR_P(tracked) && DECL_DECOMPOSITION_P(tracked) && !DECL_DECOMP_IS_BASE(tracked)
                    && DECL_DECOMP_BASE(tracked) == decl) {
                    set_object(state, tracked_slot, Status::live);
                    slots_[tracked_slot].is_lent = slots_[tracked_slot].is_lent || lends_bindings;
                }
            }
        }
    }

    // A loop: a fixed point of the state at its head.  MAY_SKIP is true for a
    // range-based loop, whose body can run zero times.
    void walk_loop(tree condition, tree body, tree step, bool is_do, bool may_skip, FlowState& state) {
        tree stripped = strip_conversions(condition);
        bool is_constant = stripped != NULL_TREE && TREE_CODE(stripped) == INTEGER_CST;
        bool is_always_true = !may_skip && (condition == NULL_TREE || (is_constant && !integer_zerop(stripped)));
        bool is_always_false = is_constant && integer_zerop(stripped);
        FlowState entry = state;
        FlowState head = entry;
        FlowState exit = unreachable_state();
        for (int pass = 0; pass < kLoopPasses; ++pass) {
            Target target;
            target.scope_depth = scopes_.size();
            target.is_loop = true;
            targets_.push_back(&target);
            FlowState current = head;
            FlowState leaving = unreachable_state();
            if (!is_do) {
                if (!may_skip) {
                    walk_statement(condition, current);
                }
                if (!is_always_true) {
                    leaving = join(leaving, current);
                }
                if (is_always_false) {
                    current.is_reachable = false;
                }
                walk_statement(body, current);
                for (const FlowState& continued : target.continues) {
                    current = join(current, continued);
                }
                walk_statement(step, current);
            } else {
                walk_statement(body, current);
                for (const FlowState& continued : target.continues) {
                    current = join(current, continued);
                }
                walk_statement(condition, current);
                if (!is_always_true) {
                    leaving = join(leaving, current);
                }
                if (is_always_false) {
                    current.is_reachable = false;
                }
            }
            for (const FlowState& broken : target.breaks) {
                leaving = join(leaving, broken);
            }
            targets_.pop_back();
            exit = leaving;
            FlowState next_head = join(entry, current);
            if (is_same_state(next_head, head)) {
                break;
            }
            head = next_head;
        }
        state = exit;
    }

    void walk_switch(tree node, FlowState& state) {
        walk_statement(SWITCH_STMT_COND(node), state);
        SwitchContext context;
        context.entry = state;
        Target target;
        target.scope_depth = scopes_.size();
        target.is_loop = false;
        targets_.push_back(&target);
        switches_.push_back(&context);
        FlowState current = unreachable_state();
        walk_statement(SWITCH_STMT_BODY(node), current);
        switches_.pop_back();
        targets_.pop_back();
        FlowState exit = current;
        for (const FlowState& broken : target.breaks) {
            exit = join(exit, broken);
        }
        if (!context.has_default) {
            exit = join(exit, context.entry);
        }
        state = exit;
    }

    // ── Expressions ──

    void walk_expression(tree node, FlowState& state) {
        if (node == NULL_TREE || node == error_mark_node) {
            return;
        }
        if (DECL_P(node) || TYPE_P(node) || CONSTANT_CLASS_P(node) || TREE_CODE(node) == IDENTIFIER_NODE) {
            return;
        }
        if (tree original = unfolded(node, NULL_TREE); original != node) {
            open_originals_.push_back(original);
            walk_expression(original, state);
            open_originals_.pop_back();
            return;
        }
        if (walk_kept_assignment(node, state)) {
            return;
        }
        switch (TREE_CODE(node)) {
            case COND_EXPR: {
                walk_expression(TREE_OPERAND(node, 0), state);
                FlowState one = state;
                FlowState other = state;
                walk_expression(TREE_OPERAND(node, 1), one);
                walk_expression(TREE_OPERAND(node, 2), other);
                state = join(one, other);
                return;
            }
            case TRUTH_ANDIF_EXPR:
            case TRUTH_ORIF_EXPR: {
                walk_expression(TREE_OPERAND(node, 0), state);
                FlowState taken = state;
                walk_expression(TREE_OPERAND(node, 1), taken);
                state = join(state, taken);
                return;
            }
            case STMT_EXPR:
                walk_statement(STMT_EXPR_STMT(node), state);
                return;
            case BIND_EXPR:
            case STATEMENT_LIST:
            case DECL_EXPR:
            case RETURN_EXPR:
            case IF_STMT:
            case CLEANUP_STMT:
                walk_statement(node, state);
                return;
            case TARGET_EXPR:
                walk_expression(TARGET_EXPR_INITIAL(node), state);
                return;
            case EXPR_STMT:
                // An expression statement inside a CLEANUP_POINT_EXPR.
                walk_expression(EXPR_STMT_EXPR(node), state);
                resolve_orphans(node, state);
                return;
            case CALL_EXPR:
            case AGGR_INIT_EXPR:
                walk_call(node, state);
                return;
            case INIT_EXPR:
            case MODIFY_EXPR:
                walk_assignment(TREE_OPERAND(node, 0), TREE_OPERAND(node, 1), TREE_CODE(node) == INIT_EXPR,
                                location_or(node, function_location()), state);
                return;
            case CONSTRUCTOR:
                walk_constructor(node, state);
                return;
            case THROW_EXPR:
                walk_expression(TREE_OPERAND(node, 0), state);
                state.is_reachable = false;
                return;
            case SIZEOF_EXPR:
            case ALIGNOF_EXPR:
            case NOEXCEPT_EXPR:
            case REQUIRES_EXPR:
            case LAMBDA_EXPR:
            case REFLECT_EXPR:
                return;
            case TREE_LIST:
                for (tree item = node; item != NULL_TREE; item = TREE_CHAIN(item)) {
                    walk_expression(TREE_VALUE(item), state);
                }
                return;
            default:
                break;
        }
        if (EXPR_P(node) || STATEMENT_CLASS_P(node)) {
            int length = TREE_OPERAND_LENGTH(node);
            for (int index = 0; index < length; ++index) {
                walk_expression(TREE_OPERAND(node, index), state);
            }
        }
    }

    // A trivial assignment of an empty class whose source has a side effect,
    // and stays in the tree as the first operand of a comma expression (the
    // folds).  The event gave its target.  Returns false for each other node.
    bool walk_kept_assignment(tree node, FlowState& state) {
        auto found = empty_assignment_of_source.find(node);
        if (found == empty_assignment_of_source.end()
            || std::find(open_assignments_.begin(), open_assignments_.end(), node) != open_assignments_.end()) {
            return false;
        }
        const EmptyAssignment& assignment = empty_assignments[found->second];
        open_assignments_.push_back(node);
        walk_assignment(assignment.target, node, false, location_or(node, assignment.location), state);
        open_assignments_.pop_back();
        return true;
    }

    // A statement whose expression the front end replaced with a void
    // constant: each trivial assignment of an empty class whose source had no
    // side effect, at a location in the statement (the folds).
    void walk_removed_assignments(tree statement, FlowState& state) {
        if (!EXPR_HAS_LOCATION(statement)) {
            return;
        }
        location_t start = expansion_point(EXPR_LOCATION(statement));
        location_t end = next_statement_ != UNKNOWN_LOCATION ? expansion_point(next_statement_) : UNKNOWN_LOCATION;
        for (std::size_t index = 0; index < empty_assignments.size(); ++index) {
            const EmptyAssignment& assignment = empty_assignments[index];
            location_t where = expansion_point(assignment.location);
            bool is_after_start = linemap_compare_locations(line_table, start, where) >= 0;
            bool is_before_end = end == UNKNOWN_LOCATION || linemap_compare_locations(line_table, where, end) > 0;
            if (is_after_start && is_before_end) {
                open_assignments_.push_back(assignment.source);
                walk_assignment(assignment.target, assignment.source, false, assignment.location, state);
                open_assignments_.pop_back();
            }
        }
    }

    static location_t expansion_point(location_t location) {
        return linemap_resolve_location(line_table, location, LRK_MACRO_EXPANSION_POINT, nullptr);
    }

    static bool is_void_constant(tree node) {
        node = node != NULL_TREE ? tree_strip_any_location_wrapper(node) : node;
        return node != NULL_TREE && TREE_CODE(node) == VOID_CST;
    }

    void walk_call(tree call, FlowState& state) {
        bool is_aggregate = TREE_CODE(call) == AGGR_INIT_EXPR;
        tree callee = cp_get_callee_fndecl_nofold(call);
        if (callee != NULL_TREE && TREE_CODE(callee) != FUNCTION_DECL) {
            callee = NULL_TREE;
        }
        refill_at_call(call, callee, state);
        if (!is_aggregate) {
            walk_expression(CALL_EXPR_FN(call), state);
        }
        int count = argument_count(call);
        for (int index = 0; index < count; ++index) {
            walk_expression(argument_at(call, index), state);
        }
        if (is_std_move_cast(callee)) {
            return;
        }
        location_t location = location_or(call, function_location());
        tree type = function_type_of(call, callee);
        tree parameter = type != NULL_TREE ? TYPE_ARG_TYPES(type) : NULL_TREE;
        bool is_method = type != NULL_TREE && TREE_CODE(type) == METHOD_TYPE;
        bool is_constructor = callee != NULL_TREE && DECL_CONSTRUCTOR_P(callee);
        bool is_assignment = callee != NULL_TREE && DECL_ASSIGNMENT_OPERATOR_P(callee);
        // A move assignment of a linear object from itself moves no value, so
        // the states stay (the self-moves).
        if (is_method && count == 2 && is_move_assignment_call(call)
            && is_self_move(argument_at(call, 0), argument_at(call, 1))) {
            if (state.is_reachable) {
                add_pending(Kind::linear_self_move, location, self_move_entity(argument_at(call, 0)));
            }
            return;
        }
        for (int index = 0; index < count; ++index) {
            tree parameter_type = parameter != NULL_TREE ? TREE_VALUE(parameter) : NULL_TREE;
            if (parameter != NULL_TREE) {
                parameter = TREE_CHAIN(parameter);
            }
            tree argument = argument_at(call, index);
            bool saw_rvalue = false;
            tree field = NULL_TREE;
            bool is_rvalue_object = is_method && index == 0 && FUNCTION_RVALUE_QUALIFIED(type);
            tree decl = strip_to_tracked(argument, saw_rvalue, &field,
                                         is_rvalue_object ? BaseStep::whole_object : BaseStep::no_slot);
            if (is_method && index == 0) {
                walk_object_argument(decl, field, argument, type, callee, is_constructor, is_assignment, location,
                                     state);
                continue;
            }
            if (parameter_type == NULL_TREE) {
                continue;
            }
            if (decl == NULL_TREE) {
                if (TYPE_REF_P(parameter_type) && !TYPE_REF_IS_RVALUE(parameter_type)) {
                    record_discarded(location, linear_temporary_type(argument));
                }
                bool const takes_value = (TYPE_REF_P(parameter_type) && TYPE_REF_IS_RVALUE(parameter_type))
                                      || (!TYPE_REF_P(parameter_type) && CLASS_TYPE_P(parameter_type));
                if (takes_value) {
                    check_borrow_route(argument, location, state);
                }
                // The aliases: the result of a callee that gives a sum back,
                // which a callee that can keep it takes by a reference that
                // writes.
                if (is_writing_parameter(parameter_type)) {
                    for (tree sum : sums_given_back(argument)) {
                        if (can_keep_reference(callee)) {
                            note_kept_alias(sum, NULL_TREE, state);
                        }
                    }
                }
                continue;
            }
            // A reference that is not const, or an address, lets the callee
            // fill a tracked sum (the sums).
            bool is_mutable_borrow =
                (TYPE_REF_P(parameter_type) && !TYPE_REF_IS_RVALUE(parameter_type)) || TYPE_PTR_P(parameter_type);
            if (is_mutable_borrow && !TYPE_READONLY(TREE_TYPE(parameter_type))) {
                note_can_be_full(decl, field, state);
                if (std::size_t slot = sum_slot(decl, field); slot != kNoParent) {
                    slots_[slot].is_borrowed = true;
                    // The summaries: a callee that can keep the reference can
                    // fill the sum at each later point.
                    if (can_keep_reference(callee)) {
                        note_kept_alias(decl, field, state);
                    }
                }
            }
            bool is_rvalue_reference = TYPE_REF_P(parameter_type) && TYPE_REF_IS_RVALUE(parameter_type);
            if (is_rvalue_reference || (!TYPE_REF_P(parameter_type) && CLASS_TYPE_P(parameter_type))) {
                consume(decl, field, state, location);
            }
        }
        if (state.is_reachable) {
            check_closure_calls(call, callee, location);
        }
        bool is_once_door = callee != NULL_TREE && carries_linear_annotation(callee, linear_names.once_door);
        // The summaries: the call of a closure runs its body, and a call that
        // takes a closure can run it, also at a later call.
        if (callee != NULL_TREE && LAMBDA_FUNCTION_P(callee)) {
            apply_closure_call(callee, location, state);
        }
        // The once door calls the closure that it takes one time, and each
        // other callee can call it at each later point (the doors).
        for (int index = 0; index < count; ++index) {
            tree argument_type = TREE_TYPE(argument_at(call, index));
            argument_type =
                argument_type != NULL_TREE && TYPE_REF_P(argument_type) ? TREE_TYPE(argument_type) : argument_type;
            if (argument_type == NULL_TREE || !LAMBDA_TYPE_P(argument_type)) {
                continue;
            }
            tree call_operator = is_once_door ? lambda_function(argument_type) : NULL_TREE;
            if (call_operator != NULL_TREE) {
                apply_closure_call(call_operator, location, state);
            } else {
                apply_closure_fills(argument_type, ClosureCall::later_too, nullptr, state);
            }
        }
        if (callee != NULL_TREE && TREE_THIS_VOLATILE(callee)) {
            state.is_reachable = false;
        }
    }

    // The closures that move a capture: a call of such a closure, and a call
    // of the once door that takes such a closure, needs an object that the
    // frame gives up (is_object_given_up).  A call operator that gives a
    // capture of its own closure to the once door moves a capture out too,
    // so the walk notes it, and the calls of that closure meet the same test.
    void check_closure_calls(tree call, tree callee, location_t location) {
        int count = argument_count(call);
        if (calls_moving_closure(call) && count >= 1 && !is_object_given_up(argument_at(call, 0))) {
            add_pending(Kind::linear_closure_move_out, location,
                        closure_entity(argument_at(call, 0), DECL_CONTEXT(callee)));
        }
        if (callee == NULL_TREE || !carries_linear_annotation(callee, linear_names.once_door)) {
            return;
        }
        // Closures that a call owns: the once door ends each closure that
        // it takes.
        for (int index = 0; index < count; ++index) {
            report_owned_closure(TREE_TYPE(argument_at(call, index)));
        }
        // The named bodies: the first argument of the once door is its body,
        // and each other argument goes to the body.
        if (count >= 1) {
            report_owned_body(TREE_TYPE(argument_at(call, 0)));
        }
        for (int index = 0; index < count; ++index) {
            tree argument = argument_at(call, index);
            if (!is_moving_closure_type(TREE_TYPE(argument)) || is_object_given_up(argument)) {
                continue;
            }
            if (names_own_capture(argument)) {
                gives_capture_to_once_door_ = true;
                continue;
            }
            add_pending(Kind::linear_closure_move_out, location, closure_entity(argument, TREE_TYPE(argument)));
        }
    }

    // Closures that a call owns: the kind capture-unconsumed at the place
    // of the lambda of CLOSURE_TYPE, or of the type that the reference
    // CLOSURE_TYPE names, when its call operator leaves a linear capture by
    // value live on a path.  Each other type gives nothing.
    void report_owned_closure(tree closure_type) {
        if (closure_type != NULL_TREE && TYPE_REF_P(closure_type)) {
            closure_type = TREE_TYPE(closure_type);
        }
        if (closure_type == NULL_TREE || !LAMBDA_TYPE_P(closure_type)) {
            return;
        }
        closure_type = TYPE_MAIN_VARIANT(closure_type);
        tree call_operator = lambda_function(closure_type);
        bool const has_fact = call_operator != NULL_TREE && closure_summarized.count(DECL_UID(call_operator)) != 0;
        bool const leaves_capture =
            call_operator != NULL_TREE && closure_leaves_capture.count(DECL_UID(call_operator)) != 0;
        // The generic closures: a call operator template, or a call operator
        // whose body the unit reads after this walk, has no fact here.  So a
        // closure with a linear capture by value waits for the end of the unit.
        if (!leaves_capture && (has_fact || !has_owned_linear_capture(closure_type))) {
            return;
        }
        tree lambda = CLASSTYPE_LAMBDA_EXPR(closure_type);
        location_t where =
            lambda != NULL_TREE ? LAMBDA_EXPR_LOCATION(lambda) : DECL_SOURCE_LOCATION(TYPE_NAME(closure_type));
        tree holder = decl_function_context(TYPE_NAME(closure_type));
        std::string owner = holder != NULL_TREE ? function_text(holder) : std::string{"namespace scope"};
        std::string entity = owner + ": closure (" + linear_type_text(closure_type) + ")";
        if (leaves_capture) {
            add_pending(Kind::linear_capture_unconsumed, where, entity);
            return;
        }
        pending_.push_back(PendingFinding{Kind::linear_capture_unconsumed, where, std::move(entity), DoorEnd::exempt,
                                          TYPE_UID(closure_type)});
    }

    // The named bodies: the kind capture-unconsumed at the name of the class
    // BODY_TYPE, or of the class that the reference BODY_TYPE names, when it
    // is no closure, holds a linear value, and its call operator && leaves a
    // linear field live on a path.  A body whose call operator has no fact at
    // the walk waits for the end of the unit.
    void report_owned_body(tree body_type) {
        if (body_type != NULL_TREE && TYPE_REF_P(body_type)) {
            body_type = TREE_TYPE(body_type);
        }
        if (body_type == NULL_TREE) {
            return;
        }
        body_type = TYPE_MAIN_VARIANT(body_type);
        if (!CLASS_TYPE_P(body_type) || LAMBDA_TYPE_P(body_type) || !COMPLETE_TYPE_P(body_type)
            || TYPE_NAME(body_type) == NULL_TREE) {
            return;
        }
        TypeFacts const facts = facts_of(body_type);
        if (facts.linearity == Linearity::none || facts.ends_at_scope) {
            return;
        }
        unsigned const body = TYPE_UID(body_type);
        bool const leaves_field = closure_types_leave_capture.count(body) != 0;
        if (!leaves_field && named_bodies_summarized.count(body) != 0) {
            return;
        }
        location_t where = DECL_SOURCE_LOCATION(TYPE_NAME(body_type));
        std::string entity = "named body (" + linear_type_text(body_type) + ")";
        if (leaves_field) {
            add_pending(Kind::linear_capture_unconsumed, where, entity);
            return;
        }
        pending_.push_back(
            PendingFinding{Kind::linear_capture_unconsumed, where, std::move(entity), DoorEnd::exempt, body, true});
    }

    // True when the closure CLOSURE_TYPE holds a linear capture by value (the
    // closures that a call owns).  Complexity: linear in the fields.
    static bool has_owned_linear_capture(tree closure_type) {
        for (tree field = TYPE_FIELDS(closure_type); field != NULL_TREE; field = DECL_CHAIN(field)) {
            if (is_owned_linear_capture(field)) {
                return true;
            }
        }
        return false;
    }

    // True when OBJECT names a capture of the closure of the call operator
    // that the walk reads, through std::move, std::forward and the casts (the
    // closures that move a capture).
    bool names_own_capture(tree object) const {
        tree closure = DECL_CONTEXT(function_);
        if (closure == NULL_TREE || !LAMBDA_TYPE_P(closure)) {
            return false;
        }
        for (int depth = 0; object != NULL_TREE && depth < kTypeDepth; ++depth) {
            object = tree_strip_any_location_wrapper(object);
            tree_code code = TREE_CODE(object);
            if (code == NOP_EXPR || code == CONVERT_EXPR || code == NON_LVALUE_EXPR || code == VIEW_CONVERT_EXPR
                || code == STATIC_CAST_EXPR) {
                object = TREE_OPERAND(object, 0);
            } else if (code == CALL_EXPR && call_expr_nargs(object) >= 1
                       && is_std_move_cast(cp_get_callee_fndecl_nofold(object))) {
                object = CALL_EXPR_ARG(object, 0);
            } else {
                break;
            }
        }
        return capture_field_of(object, TYPE_MAIN_VARIANT(closure)) != NULL_TREE;
    }

    // FUNCTION: OBJECT (TYPE) for a call that takes the closure OBJECT of the
    // type CLOSURE_TYPE, with the name of the object of the closure or the
    // word closure (the closures that move a capture).
    std::string closure_entity(tree object, tree closure_type) const {
        bool saw_rvalue = false;
        tree named = strip_to_tracked(object, saw_rvalue);
        if (named == NULL_TREE) {
            named = self_move_place(object);
        }
        bool is_named = named != NULL_TREE && (TREE_CODE(named) == VAR_DECL || TREE_CODE(named) == PARM_DECL);
        if (closure_type != NULL_TREE && TYPE_REF_P(closure_type)) {
            closure_type = TREE_TYPE(closure_type);
        }
        return function_text(function_) + ": " + (is_named ? simple_name(named) : std::string{"closure"}) + " ("
             + linear_type_text(closure_type) + ")";
    }

    // The summaries: true when the callee CALLEE can keep a reference
    // parameter that names a linear sum.  A callee with no fact at this point
    // keeps it in the walk that reads such a callee as one that can keep, and
    // the walk notes the callee for the end of the unit.
    bool can_keep_reference(tree callee) {
        KeptFact fact = kept_fact_of(callee);
        if (fact != KeptFact::unknown) {
            return fact == KeptFact::can_keep;
        }
        unsigned uid = DECL_UID(callee);
        if (std::find(unknown_callees_.begin(), unknown_callees_.end(), uid) == unknown_callees_.end()) {
            unknown_callees_.push_back(uid);
        }
        return unknown_callee_ == UnknownCallee::can_keep;
    }

    // The aliases: the tracked sums that NODE names through the result of a
    // callee that gives a reference parameter back: each sum that the call
    // takes by a reference that writes.
    std::vector<tree> sums_given_back(tree node) const {
        std::vector<tree> sums;
        for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
            node = tree_strip_any_location_wrapper(node);
            switch (TREE_CODE(node)) {
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case ADDR_EXPR:
                case INDIRECT_REF:
                case CLEANUP_POINT_EXPR:
                    node = TREE_OPERAND(node, 0);
                    continue;
                case CALL_EXPR:
                    break;
                default:
                    return sums;
            }
            tree callee = cp_get_callee_fndecl_nofold(node);
            if (is_std_move_cast(callee) && call_expr_nargs(node) >= 1) {
                node = CALL_EXPR_ARG(node, 0);
                continue;
            }
            if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL
                || returns_sum_reference.count(DECL_UID(callee)) == 0) {
                return sums;
            }
            tree parameter = TYPE_ARG_TYPES(TREE_TYPE(callee));
            for (int index = 0; index < argument_count(node); ++index) {
                tree parameter_type = parameter != NULL_TREE ? TREE_VALUE(parameter) : NULL_TREE;
                if (parameter != NULL_TREE) {
                    parameter = TREE_CHAIN(parameter);
                }
                bool saw_rvalue = false;
                tree field = NULL_TREE;
                tree decl = strip_to_tracked(argument_at(node, index), saw_rvalue, &field);
                if (is_writing_parameter(parameter_type) && sum_slot(decl, field) != kNoParent) {
                    sums.push_back(decl);
                }
            }
            return sums;
        }
        return sums;
    }

    // The aliases: a reference or a pointer that the result of a callee that
    // gives a reference parameter back initializes names each such sum.
    void note_given_back_alias(tree decl, tree value, FlowState& state) {
        tree type = decl != NULL_TREE ? TREE_TYPE(decl) : NULL_TREE;
        if (type == NULL_TREE || !(TYPE_REF_P(type) || TYPE_PTR_P(type)) || TYPE_READONLY(TREE_TYPE(type))) {
            return;
        }
        for (tree sum : sums_given_back(value)) {
            note_kept_alias(sum, NULL_TREE, state);
        }
    }

    // The aliases: a call that starts after the consume of a sum that an
    // alias names can fill the sum again through the alias, so the sum holds
    // a payload again.  The call of a member of a linear sum on a tracked
    // object changes only that object, and std::move changes nothing.
    void refill_at_call(tree call, tree callee, FlowState& state) {
        if (!state.is_reachable || is_std_move_cast(callee)) {
            return;
        }
        if (callee != NULL_TREE && TREE_CODE(TREE_TYPE(callee)) == METHOD_TYPE && is_linear_sum(DECL_CONTEXT(callee))
            && argument_count(call) >= 1) {
            bool saw_rvalue = false;
            tree field = NULL_TREE;
            if (strip_to_tracked(argument_at(call, 0), saw_rvalue, &field) != NULL_TREE) {
                return;
            }
            // A member that cannot fill a sum fills none through an alias: a
            // consumer, a member that empties, and a const member.
            tree type = TREE_TYPE(callee);
            tree object_type = TYPE_ARG_TYPES(type) != NULL_TREE ? TREE_VALUE(TYPE_ARG_TYPES(type)) : NULL_TREE;
            bool is_const_object =
                object_type != NULL_TREE && INDIRECT_TYPE_P(object_type) && TYPE_READONLY(TREE_TYPE(object_type));
            if (FUNCTION_RVALUE_QUALIFIED(type) || is_const_object
                || carries_linear_annotation(callee, linear_names.empties)) {
                return;
            }
            // A member that can fill a sum, on the result of a callee that
            // gives the sum back, fills that sum.
            for (tree sum : sums_given_back(argument_at(call, 0))) {
                std::size_t slot = sum_slot(sum, NULL_TREE);
                if (state.get(slot) == Status::consumed || state.get(slot) == Status::maybe) {
                    set_object(state, slot, Status::live);
                }
                state.set_content(slot, Content::can_be_full);
            }
        }
        for (std::size_t slot = 0; slot < slots_.size(); ++slot) {
            if (!slots_[slot].can_refill_at_calls) {
                continue;
            }
            Status status = state.get(slot);
            if (status == Status::consumed || status == Status::maybe) {
                set_object(state, slot, Status::live);
            }
            if (status != Status::dead) {
                state.set_content(slot, Content::can_be_full);
            }
        }
    }

    // Whether a closure runs only at the call that the walk reads, or can run
    // at a later call too, because a callee took it and can keep it.
    enum class ClosureCall : std::uint8_t {
        here,
        later_too,
    };

    // The summaries: the call of the call operator OPERATOR leaves each sum
    // that each path of its body leaves empty consumed, and each other sum
    // that the body can fill holds a payload again.
    void apply_closure_call(tree call_operator, location_t location, FlowState& state) {
        auto empties = closure_empties.find(DECL_UID(call_operator));
        const std::vector<unsigned>* emptied = empties != closure_empties.end() ? &empties->second : nullptr;
        tree closure = DECL_CONTEXT(call_operator);
        if (emptied != nullptr && closure != NULL_TREE && TYPE_P(closure) && state.is_reachable) {
            auto targets = capture_targets_.find(TYPE_UID(TYPE_MAIN_VARIANT(closure)));
            if (targets != capture_targets_.end()) {
                for (const auto& [field, decl] : targets->second) {
                    if (std::find(emptied->begin(), emptied->end(), field) != emptied->end()) {
                        consume(decl, NULL_TREE, state, location);
                    }
                }
            }
        }
        apply_closure_fills(closure, ClosureCall::here, emptied, state);
    }

    // The summaries: the body of the closure CLOSURE_TYPE can fill each sum
    // that it captures by reference and that its summary names, so the sum
    // holds a payload again after the call.  A closure with no summary can
    // fill each such sum.  A capture that EMPTIED names ends empty.  A
    // closure that a callee can keep can fill the sum at each later call.
    void apply_closure_fills(tree closure_type, ClosureCall when, const std::vector<unsigned>* emptied,
                             FlowState& state) {
        if (closure_type == NULL_TREE || !TYPE_P(closure_type) || !state.is_reachable) {
            return;
        }
        unsigned uid = TYPE_UID(TYPE_MAIN_VARIANT(closure_type));
        auto targets = capture_targets_.find(uid);
        if (targets == capture_targets_.end()) {
            return;
        }
        auto fills = closure_fills.find(uid);
        for (const auto& [field, decl] : targets->second) {
            bool can_fill = fills == closure_fills.end()
                         || std::find(fills->second.begin(), fills->second.end(), field) != fills->second.end();
            bool is_emptied =
                emptied != nullptr && std::find(emptied->begin(), emptied->end(), field) != emptied->end();
            std::size_t slot = sum_slot(decl, NULL_TREE);
            if (!can_fill || is_emptied || slot == kNoParent) {
                continue;
            }
            if (state.get(slot) == Status::consumed || state.get(slot) == Status::maybe) {
                set_object(state, slot, Status::live);
            }
            state.set_content(slot, Content::can_be_full);
            if (when == ClosureCall::later_too) {
                slots_[slot].can_refill_at_calls = true;
            }
        }
    }

    // The object argument of a call of a member function: an assignment
    // assigns over it, and a member function with the qualifier && consumes
    // it.
    void walk_object_argument(tree decl, tree field, tree argument, tree type, tree callee, bool is_constructor,
                              bool is_assignment, location_t location, FlowState& state) {
        if (is_constructor) {
            return;
        }
        if (decl == NULL_TREE) {
            // Closures that a call owns: a temporary closure whose call
            // operator ends each linear capture on each path drops nothing at
            // the end of its full expression.
            bool const ends_each_capture = callee != NULL_TREE && LAMBDA_FUNCTION_P(callee)
                                        && closure_summarized.count(DECL_UID(callee)) != 0
                                        && closure_leaves_capture.count(DECL_UID(callee)) == 0;
            if (!FUNCTION_RVALUE_QUALIFIED(type) && !ends_each_capture) {
                record_discarded(location, linear_temporary_type(argument));
            }
            if (FUNCTION_RVALUE_QUALIFIED(type) && !is_assignment) {
                check_borrow_route(argument, location, state);
            }
            return;
        }
        check_known_content(decl, field, callee, location, state);
        if (is_assignment) {
            // The rule drop-door reads an assignment to a field (the shapes).
            if (field == NULL_TREE) {
                assign_over(decl, state, location);
            } else {
                refill_lent_field(decl, field, state);
            }
        } else if (FUNCTION_RVALUE_QUALIFIED(type) || (callee != NULL_TREE && DECL_DESTRUCTOR_P(callee))) {
            consume(decl, field, state, location);
        } else {
            note_sum_member(decl, field, type, callee, state);
        }
    }

    // An initialization (IS_INIT) or an assignment of TARGET from VALUE.
    void walk_assignment(tree target, tree value, bool is_init, location_t location, FlowState& state) {
        tree stripped_target = target;
        while (stripped_target != NULL_TREE && TREE_CODE(stripped_target) == INDIRECT_REF) {
            stripped_target = TREE_OPERAND(stripped_target, 0);
        }
        // The objects: the initialization of a reference that extends a
        // linear temporary starts the object of the reference.
        if (is_init && stripped_target != NULL_TREE && slot_of_.count(stripped_target) == 0
            && binds_extended_temporary(stripped_target, value)) {
            track_extending(stripped_target);
        }
        if (is_init && stripped_target != NULL_TREE && VAR_P(stripped_target)) {
            note_given_back_alias(stripped_target, value, state);
            tree reference_type = TREE_TYPE(stripped_target);
            if (reference_type != NULL_TREE && TYPE_REF_P(reference_type) && !TYPE_REF_IS_RVALUE(reference_type)) {
                reference_initial_[stripped_target] = value;
            }
        }
        tree tracked_target =
            stripped_target != NULL_TREE && slot_of_.count(stripped_target) != 0 ? stripped_target : NULL_TREE;
        value = unfolded(value, stripped_target);
        open_originals_.push_back(value);
        walk_expression(value, state);
        if (tracked_target == NULL_TREE) {
            walk_expression(target, state);
        }
        bool saw_rvalue = false;
        tree field = NULL_TREE;
        tree source = strip_to_tracked(value, saw_rvalue, &field);
        open_originals_.pop_back();
        bool is_linear_target =
            target != NULL_TREE && TREE_TYPE(target) != NULL_TREE && is_linear_or_reference(TREE_TYPE(target));
        // A move assignment of a linear object from itself moves no value, so
        // the states stay (the self-moves).
        bool is_linear_class_target =
            target != NULL_TREE && TREE_TYPE(target) != NULL_TREE && is_linear_type(TREE_TYPE(target));
        if (!is_init && is_linear_class_target && is_self_move(target, value)) {
            if (state.is_reachable) {
                add_pending(Kind::linear_self_move, location, self_move_entity(target));
            }
            return;
        }
        // The captures: a lambda initializes a capture by reference with the
        // address of the object, and the closure hides each later consume.
        bool is_reference_capture = target != NULL_TREE && TREE_CODE(target) == COMPONENT_REF
                                 && LAMBDA_TYPE_P(TREE_TYPE(TREE_OPERAND(target, 0))) && TREE_TYPE(target) != NULL_TREE
                                 && TYPE_REF_P(TREE_TYPE(target));
        // An address of a tracked sum in a pointer that is not const, and a
        // reference member that is not const, can fill the sum (the sums).
        tree target_type = target != NULL_TREE ? TREE_TYPE(target) : NULL_TREE;
        bool is_mutable_address =
            target_type != NULL_TREE
            && (TYPE_PTR_P(target_type) || (TYPE_REF_P(target_type) && TREE_CODE(target) == COMPONENT_REF))
            && !TYPE_READONLY(TREE_TYPE(target_type));
        if (source != NULL_TREE && is_reference_capture) {
            // The closure can fill a captured sum (the sums), and a call of
            // the closure applies its summary to the sum (the summaries).
            mark_escaped(source);
            note_alias(source, field, state);
            tree closure = TYPE_MAIN_VARIANT(TREE_TYPE(TREE_OPERAND(target, 0)));
            if (field == NULL_TREE && sum_slot(source, NULL_TREE) != kNoParent) {
                capture_targets_[TYPE_UID(closure)].emplace_back(DECL_UID(TREE_OPERAND(target, 1)), source);
            }
        } else if (source != NULL_TREE && source != tracked_target && is_mutable_address) {
            note_kept_alias(source, field, state);
        } else if (source == NULL_TREE && is_mutable_address) {
            // The result of a callee that gives a sum back in a stored address
            // or a reference member (the aliases).
            for (tree sum : sums_given_back(value)) {
                note_kept_alias(sum, NULL_TREE, state);
            }
        } else if (source != NULL_TREE && source != tracked_target && (saw_rvalue || is_linear_target)
                   && !is_lent_borrow(source, saw_rvalue, target_type)) {
            consume(source, field, state, location);
        } else if (source == NULL_TREE && target_type != NULL_TREE && !TYPE_REF_P(target_type)
                   && !TYPE_PTR_P(target_type)) {
            check_borrow_route(value, location, state);
        }
        if (tracked_target != NULL_TREE && state.is_reachable) {
            if (is_init) {
                set_object(state, slot_of_.at(tracked_target), Status::live);
                note_initial_content(tracked_target, value, state);
            } else {
                assign_over(tracked_target, state, location);
            }
        } else if (!is_init && target != NULL_TREE) {
            bool saw_target_rvalue = false;
            tree target_field = NULL_TREE;
            tree owner = strip_to_tracked(target, saw_target_rvalue, &target_field);
            if (owner != NULL_TREE && target_field != NULL_TREE) {
                refill_lent_field(owner, target_field, state);
            }
        }
    }

    // The lent parameters: an lvalue reference, such as the result of a
    // function that gives the lent object back by reference, borrows the lent
    // object SOURCE when no move cast is on the way, so it consumes nothing.
    bool is_lent_borrow(tree source, bool saw_rvalue, tree target_type) const {
        return !saw_rvalue && target_type != NULL_TREE && TYPE_REF_P(target_type) && !TYPE_REF_IS_RVALUE(target_type)
            && slots_[slot_of_.at(source)].is_lent;
    }

    // The moves through a borrow: true when INITIAL, the initializer of a
    // local reference, gives the address of a temporary or of the hidden
    // variable of one.
    static bool binds_temporary(tree initial) {
        tree node = initial;
        bool saw_address = false;
        for (int depth = 0; node != NULL_TREE && node != error_mark_node && depth < kTypeDepth; ++depth) {
            node = tree_strip_any_location_wrapper(node);
            switch (TREE_CODE(node)) {
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case CLEANUP_POINT_EXPR:
                    node = TREE_OPERAND(node, 0);
                    continue;
                case COMPOUND_EXPR:
                    node = TREE_OPERAND(node, 1);
                    continue;
                case ADDR_EXPR:
                    saw_address = true;
                    node = TREE_OPERAND(node, 0);
                    continue;
                case TARGET_EXPR:
                    return true;
                case VAR_DECL:
                    return saw_address && DECL_ARTIFICIAL(node) && !TREE_STATIC(node) && !DECL_DECOMPOSITION_P(node)
                        && !DECL_HAS_VALUE_EXPR_P(node);
                default:
                    return false;
            }
        }
        return false;
    }

    // The moves through a borrow: true when CALL calls a storage door of the
    // base (moves_own_storage) on the object of this function.  A member door
    // needs an object that is the object parameter or a member of it, as
    // slot_.payload_() of an Option.  A static door needs a caller that is a
    // member of the same class, as the steps of a factory that read their
    // own slots.  A door on another object names a place of another frame.
    bool is_own_storage_call(tree call, tree callee) const {
        if (!moves_own_storage(callee)) {
            return false;
        }
        bool const is_member = TREE_CODE(TREE_TYPE(callee)) == METHOD_TYPE || DECL_XOBJ_MEMBER_FUNCTION_P(callee);
        if (!is_member) {
            tree scope = DECL_CONTEXT(callee);
            tree own_scope = DECL_CONTEXT(function_);
            return scope != NULL_TREE && own_scope != NULL_TREE && TYPE_P(scope) && TYPE_P(own_scope)
                && TYPE_MAIN_VARIANT(scope) == TYPE_MAIN_VARIANT(own_scope);
        }
        tree object = call_expr_nargs(call) > 0 ? CALL_EXPR_ARG(call, 0) : NULL_TREE;
        for (int depth = 0; object != NULL_TREE && depth < kTypeDepth; ++depth) {
            object = tree_strip_any_location_wrapper(object);
            switch (TREE_CODE(object)) {
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case VIEW_CONVERT_EXPR:
                case ADDR_EXPR:
                case INDIRECT_REF:
                case COMPONENT_REF:
                    object = TREE_OPERAND(object, 0);
                    continue;
                case PARM_DECL:
                    return is_object_parameter(object);
                default:
                    return false;
            }
        }
        return false;
    }

    // The moves through a borrow: the text of a dereference of POINTER, which
    // names a place of another frame, or an empty text when the walk reads
    // on: POINTER is the object parameter or an address, a reference that
    // the front end converts through a pointer type, or a pointer that a
    // storage door of the base gives.  An index on a pointer names a place
    // of the same storage as the pointer.
    std::string dereferenced_place(tree pointer) const {
        tree stripped = tree_strip_nop_conversions(tree_strip_any_location_wrapper(pointer));
        for (int depth = 0; TREE_CODE(stripped) == POINTER_PLUS_EXPR && depth < kTypeDepth; ++depth) {
            stripped = tree_strip_nop_conversions(tree_strip_any_location_wrapper(TREE_OPERAND(stripped, 0)));
        }
        tree stripped_type = TREE_TYPE(stripped);
        bool const is_object = TREE_CODE(stripped) == PARM_DECL && is_object_parameter(stripped);
        if (is_object || TREE_CODE(stripped) == ADDR_EXPR || stripped_type == NULL_TREE || !TYPE_PTR_P(stripped_type)) {
            return std::string{};
        }
        tree named = TREE_CODE(stripped) == COMPONENT_REF ? TREE_OPERAND(stripped, 1) : stripped;
        if (TREE_CODE(named) == CALL_EXPR) {
            tree called = cp_get_callee_fndecl_nofold(named);
            if (is_own_storage_call(named, called)) {
                return std::string{};
            }
            return "*" + (called != NULL_TREE ? simple_name(called) : std::string{"call"}) + "()";
        }
        return "*" + (DECL_P(named) ? simple_name(named) : std::string{"pointer"});
    }

    // The moves through a borrow: the text of the place that VALUE moves a
    // linear value out of, when no frame of this function owns that place,
    // or an empty text.  The walk goes from VALUE to the object that holds
    // the place, through the casts, std::move, a reference helper, a member,
    // an element, a binding of a structured binding, a local lvalue reference
    // to its initializer, and a call that gives a reference to its object or
    // to its first argument that refers to a linear class.  Complexity: at
    // most kTypeDepth steps.
    std::string borrowed_place(tree value) const {
        bool saw_rvalue = false;
        bool is_behind_borrow = false;
        std::string place;
        for (int depth = 0; value != NULL_TREE && depth < kTypeDepth; ++depth) {
            value = tree_strip_any_location_wrapper(unfolded(value, NULL_TREE));
            switch (TREE_CODE(value)) {
                case NOP_EXPR:
                case CONVERT_EXPR:
                case NON_LVALUE_EXPR:
                case VIEW_CONVERT_EXPR:
                case IMPLICIT_CONV_EXPR:
                case STATIC_CAST_EXPR: {
                    tree type = TREE_TYPE(value);
                    if (type != NULL_TREE && TYPE_REF_P(type) && TYPE_REF_IS_RVALUE(type)) {
                        saw_rvalue = true;
                    }
                    // A conversion of a pointer to a reference is a hidden
                    // dereference, such as the fold of &*pointer.
                    tree operand = TREE_OPERAND(value, 0);
                    bool const is_dereference = type != NULL_TREE && TYPE_REF_P(type) && TREE_TYPE(operand) != NULL_TREE
                                             && TYPE_PTR_P(TREE_TYPE(operand));
                    std::string const pointed = is_dereference ? dereferenced_place(operand) : std::string{};
                    if (!pointed.empty()) {
                        return saw_rvalue ? (place.empty() ? pointed : place) : std::string{};
                    }
                    value = operand;
                    continue;
                }
                case INDIRECT_REF: {
                    tree pointer = TREE_OPERAND(value, 0);
                    bool const is_pointer = TREE_TYPE(pointer) != NULL_TREE && TYPE_PTR_P(TREE_TYPE(pointer));
                    std::string const pointed = is_pointer ? dereferenced_place(pointer) : std::string{};
                    if (!pointed.empty()) {
                        return saw_rvalue ? (place.empty() ? pointed : place) : std::string{};
                    }
                    value = pointer;
                    continue;
                }
                case ADDR_EXPR:
                case CLEANUP_POINT_EXPR:
                case SAVE_EXPR:
                case PAREN_EXPR:
                    value = TREE_OPERAND(value, 0);
                    continue;
                case COMPOUND_EXPR: {
                    bool is_empty_argument =
                        TREE_CODE(tree_strip_any_location_wrapper(TREE_OPERAND(value, 1))) == EMPTY_CLASS_EXPR;
                    value = TREE_OPERAND(value, is_empty_argument ? 0 : 1);
                    continue;
                }
                case TARGET_EXPR:
                    value = TARGET_EXPR_INITIAL(value);
                    continue;
                case COMPONENT_REF: {
                    tree member = TREE_OPERAND(value, 1);
                    if (place.empty() && TREE_CODE(member) == FIELD_DECL && !DECL_FIELD_IS_BASE(member)) {
                        place = simple_name(member);
                    }
                    value = TREE_OPERAND(value, 0);
                    continue;
                }
                case ARRAY_REF:
                    if (place.empty()) {
                        place = "element";
                    }
                    value = TREE_OPERAND(value, 0);
                    continue;
                case CALL_EXPR: {
                    tree callee = cp_get_callee_fndecl_nofold(value);
                    if (is_std_move_cast(callee) && call_expr_nargs(value) >= 1) {
                        tree result = TREE_TYPE(value);
                        saw_rvalue = saw_rvalue || DECL_NAME(callee) != linear_names.forward
                                  || (result != NULL_TREE && TYPE_REF_P(result) && TYPE_REF_IS_RVALUE(result));
                        value = CALL_EXPR_ARG(value, 0);
                        continue;
                    }
                    int const forwarded = forwarded_parameter_index(callee);
                    if (forwarded != kNoParameter && forwarded < call_expr_nargs(value)) {
                        value = CALL_EXPR_ARG(value, forwarded);
                        continue;
                    }
                    // A call that gives a value gives a temporary.  A storage
                    // door of the base gives a place that the base owns.
                    tree result = TREE_TYPE(value);
                    if (result == NULL_TREE || !TYPE_REF_P(result)) {
                        return std::string{};
                    }
                    saw_rvalue = saw_rvalue || TYPE_REF_IS_RVALUE(result);
                    if (!saw_rvalue || is_own_storage_call(value, callee)) {
                        return std::string{};
                    }
                    is_behind_borrow = true;
                    if (place.empty()) {
                        place = (callee != NULL_TREE ? simple_name(callee) : std::string{"call"}) + "()";
                    }
                    tree next = referred_argument(value, callee);
                    // A class that holds no linear part, such as a Ref,
                    // refers to a place of another frame.
                    if (next == NULL_TREE || !is_linear_type(TREE_TYPE(TREE_TYPE(next)))) {
                        return place;
                    }
                    value = next;
                    continue;
                }
                case VAR_DECL:
                case PARM_DECL: {
                    if (!saw_rvalue) {
                        return std::string{};
                    }
                    // A binding of a structured binding names a part of its
                    // hidden object.  Each other proxy is a capture.
                    if (DECL_HAS_VALUE_EXPR_P(value)) {
                        if (VAR_P(value) && DECL_DECOMPOSITION_P(value) && !DECL_DECOMP_IS_BASE(value)) {
                            value = DECL_VALUE_EXPR(value);
                            continue;
                        }
                        return std::string{};
                    }
                    if (place.empty()) {
                        place = simple_name(value);
                    }
                    if (VAR_P(value)) {
                        tree initial = local_reference_initial(value);
                        if (initial != NULL_TREE) {
                            is_behind_borrow = true;
                            value = initial;
                            continue;
                        }
                    }
                    return is_borrowed_root(value, is_behind_borrow) ? place : std::string{};
                }
                default:
                    return std::string{};
            }
        }
        return std::string{};
    }

    // The moves through a borrow: the argument of CALL that the reference of
    // its result names: the object of a member function, or the first
    // argument of a free function that refers to a class.
    static tree referred_argument(tree call, tree callee) {
        bool const is_member =
            callee != NULL_TREE && (TREE_CODE(TREE_TYPE(callee)) == METHOD_TYPE || DECL_XOBJ_MEMBER_FUNCTION_P(callee));
        for (int index = 0; index < call_expr_nargs(call); ++index) {
            tree argument = CALL_EXPR_ARG(call, index);
            tree argument_type = TREE_TYPE(argument);
            bool const refers = argument_type != NULL_TREE && (TYPE_REF_P(argument_type) || TYPE_PTR_P(argument_type))
                             && CLASS_TYPE_P(TREE_TYPE(argument_type));
            if (refers) {
                return argument;
            }
            if (is_member) {
                return NULL_TREE;
            }
        }
        return NULL_TREE;
    }

    // The moves through a borrow: the initializer of the local lvalue
    // reference LOCAL that the walk read, or null.  A local by value, a local
    // rvalue reference and a reference that extends a temporary own their
    // object, so they give null.
    tree local_reference_initial(tree local) const {
        tree type = TREE_TYPE(local);
        if (type == NULL_TREE || !TYPE_REF_P(type) || TYPE_REF_IS_RVALUE(type) || TREE_STATIC(local)
            || DECL_EXTERNAL(local)) {
            return NULL_TREE;
        }
        auto known = reference_initial_.find(local);
        if (known == reference_initial_.end() || binds_temporary(known->second)) {
            return NULL_TREE;
        }
        return known->second;
    }

    // The moves through a borrow: true when no frame of this function owns
    // ROOT, the object at the end of the walk.  The object of a lambda, of a
    // constructor, of a destructor and of a member function && is owned, and
    // so is an explicit object parameter by value or by an rvalue reference.
    // A parameter by value or by an rvalue reference is owned, and so is a
    // local by value or by an rvalue reference.  A tracked object is owned
    // unless it is lent, or the walk reached it behind a call or a local
    // reference (IS_BEHIND_BORROW): the rule consumes no part of the object
    // for such a move, so a second move would give a second value.
    bool is_borrowed_root(tree root, bool is_behind_borrow) const {
        if (slot_of_.count(root) != 0 || referent_of_.count(root) != 0) {
            auto referent = referent_of_.find(root);
            tree object = referent != referent_of_.end() ? referent->second : root;
            bool const is_lent = slot_of_.count(object) != 0 && slots_[slot_of_.at(object)].is_lent;
            return is_behind_borrow || is_lent;
        }
        tree type = TREE_TYPE(root);
        bool const is_reference = type != NULL_TREE && TYPE_REF_P(type);
        bool const is_given_up = !is_reference || TYPE_REF_IS_RVALUE(type);
        if (TREE_CODE(root) == PARM_DECL && is_object_parameter(root)) {
            bool const is_owned_object =
                LAMBDA_FUNCTION_P(function_) || DECL_CONSTRUCTOR_P(function_) || DECL_DESTRUCTOR_P(function_)
                || (DECL_XOBJ_MEMBER_FUNCTION_P(function_) && is_given_up)
                || (DECL_IOBJ_MEMBER_FUNCTION_P(function_) && FUNCTION_RVALUE_QUALIFIED(TREE_TYPE(function_)));
            return !is_owned_object;
        }
        if (VAR_P(root) && (TREE_STATIC(root) || DECL_EXTERNAL(root) || DECL_THREAD_LOCAL_P(root))) {
            return true;
        }
        if (!is_given_up && TREE_CODE(root) == VAR_DECL && local_reference_initial(root) == NULL_TREE) {
            // A local reference that extends a temporary owns it.  One whose
            // initializer the walk did not read names a place of another
            // frame.
            auto known = reference_initial_.find(root);
            return known == reference_initial_.end() || !binds_temporary(known->second);
        }
        return !is_given_up;
    }

    // The moves through a borrow: at a consume of VALUE that names no tracked
    // object, a move of a linear lvalue out of a place that no frame of this
    // function owns gives the kind borrow-move-out (borrowed_place).  A
    // transfer function and a function with the annotation moves_own_storage
    // give none, and the body of a door gives none (the doors).
    void check_borrow_route(tree value, location_t location, const FlowState& state) {
        if (value == NULL_TREE || !state.is_reachable || purpose_ == WalkPurpose::summary || is_exempt_function()
            || moves_own_storage(function_)) {
            return;
        }
        tree type = TREE_TYPE(value);
        if (type != NULL_TREE && (TYPE_REF_P(type) || TYPE_PTR_P(type))) {
            type = TREE_TYPE(type);
        }
        if (type == NULL_TREE || !is_linear_type(type)) {
            return;
        }
        std::string const place = borrowed_place(value);
        if (place.empty()) {
            return;
        }
        add_pending(Kind::linear_borrow_move_out, location,
                    function_text(function_) + ": " + place + " (" + linear_type_text(type) + ")");
    }

    void walk_constructor(tree node, FlowState& state) {
        vec<constructor_elt, va_gc>* elements = CONSTRUCTOR_ELTS(node);
        for (unsigned index = 0; index < vec_safe_length(elements); ++index) {
            walk_expression((*elements)[index].value, state);
        }
        for (unsigned index = 0; index < vec_safe_length(elements); ++index) {
            tree value = (*elements)[index].value;
            tree field = (*elements)[index].index;
            if (value == NULL_TREE) {
                continue;
            }
            if (field != NULL_TREE && TREE_CODE(field) == FIELD_DECL && TREE_TYPE(field) != NULL_TREE
                && TYPE_REF_P(TREE_TYPE(field)) && !TYPE_REF_IS_RVALUE(TREE_TYPE(field))) {
                // The captures: a closure that captures a tracked object by
                // reference hides each later consume of it.  A reference
                // member that is not const can fill a tracked sum (the sums).
                bool saw_reference = false;
                tree referred = strip_to_tracked(value, saw_reference);
                if (referred != NULL_TREE && LAMBDA_TYPE_P(TREE_TYPE(node))) {
                    mark_escaped(referred);
                    // The summaries: a call of the closure applies its summary
                    // to this sum.
                    if (sum_slot(referred, NULL_TREE) != kNoParent) {
                        capture_targets_[TYPE_UID(TYPE_MAIN_VARIANT(TREE_TYPE(node)))].emplace_back(DECL_UID(field),
                                                                                                    referred);
                    }
                }
                // A closure fills the sum at its calls (the summaries), and a
                // reference member of each other class at each later call
                // (the aliases).
                if (referred != NULL_TREE && !TYPE_READONLY(TREE_TYPE(TREE_TYPE(field)))) {
                    if (LAMBDA_TYPE_P(TREE_TYPE(node))) {
                        note_alias(referred, NULL_TREE, state);
                    } else {
                        note_kept_alias(referred, NULL_TREE, state);
                    }
                }
                // The result of a callee that gives a sum back in a reference
                // member names the sum at each later call (the aliases).
                if (referred == NULL_TREE && !TYPE_READONLY(TREE_TYPE(TREE_TYPE(field)))) {
                    for (tree sum : sums_given_back(value)) {
                        note_kept_alias(sum, NULL_TREE, state);
                    }
                }
                continue;
            }
            // An address or a pointer in an element is no transfer.  An
            // address that is not const can fill a tracked sum at each later
            // call (the aliases).
            tree value_type = TREE_TYPE(value);
            if (value_type == NULL_TREE || TREE_CODE(value_type) == POINTER_TYPE) {
                bool saw_address = false;
                tree addressed = value_type != NULL_TREE ? strip_to_tracked(value, saw_address) : NULL_TREE;
                if (addressed != NULL_TREE && !TYPE_READONLY(TREE_TYPE(value_type))) {
                    note_kept_alias(addressed, NULL_TREE, state);
                }
                continue;
            }
            bool saw_rvalue = false;
            tree source_field = NULL_TREE;
            tree source = strip_to_tracked(value, saw_rvalue, &source_field);
            if (source != NULL_TREE && (saw_rvalue || is_linear_type(value_type))) {
                consume(source, source_field, state, location_or(value, location_or(node, function_location())));
            } else if (source == NULL_TREE && !TYPE_REF_P(value_type)) {
                check_borrow_route(value, location_or(value, location_or(node, function_location())), state);
            }
        }
    }

    // ── Discarded temporaries ──

    // A discarded temporary of the linear type TYPE.  A temporary of a linear
    // sum gives the kind option-discarded (the sums).  A null TYPE is no
    // linear temporary, and gives nothing.
    void record_discarded(location_t location, tree type) {
        if (type == NULL_TREE) {
            return;
        }
        Kind kind = is_linear_sum(type) ? Kind::linear_option_discarded : Kind::linear_discarded_temporary;
        add_pending(kind, location, function_text(function_) + ": temporary (" + linear_type_text(type) + ")",
                    door_end_of(type));
    }

    void check_discarded(tree node) {
        tree current = node;
        for (int depth = 0; current != NULL_TREE && depth < kTypeDepth; ++depth) {
            current = tree_strip_any_location_wrapper(current);
            if (TREE_CODE(current) == CLEANUP_POINT_EXPR || TREE_CODE(current) == EXPR_STMT) {
                current = TREE_OPERAND(current, 0);
                continue;
            }
            if ((TREE_CODE(current) == CONVERT_EXPR || TREE_CODE(current) == NOP_EXPR)
                && VOID_TYPE_P(TREE_TYPE(current))) {
                current = TREE_OPERAND(current, 0);
                continue;
            }
            break;
        }
        if (current == NULL_TREE) {
            return;
        }
        tree_code code = TREE_CODE(current);
        if ((code == CALL_EXPR || code == AGGR_INIT_EXPR || code == TARGET_EXPR)
            && is_dropped_temporary_type(TREE_TYPE(current))) {
            record_discarded(location_or(current, function_location()), TREE_TYPE(current));
        }
    }

    // ── The report ──

    // The kinds that the body of a door does not give (the doors).
    bool is_ended_by_door(Kind kind) const {
        return is_door_
            && (kind == Kind::linear_unconsumed || kind == Kind::linear_unconsumed_on_path
                || kind == Kind::linear_discarded_temporary || kind == Kind::linear_option_unconsumed
                || kind == Kind::linear_option_discarded || kind == Kind::linear_borrow_move_out);
    }

    void report() {
        if (purpose_ == WalkPurpose::summary) {
            return;
        }
        if (is_unanalyzed_) {
            if (!slots_.empty()) {
                record(Kind::linear_unanalyzed, function_location(), function_text(function_));
            }
            return;
        }
        for (const Slot& entry : slots_) {
            if (entry.has_spent_exit) {
                location_t where =
                    entry.first_consume != UNKNOWN_LOCATION ? entry.first_consume : DECL_SOURCE_LOCATION(entry.decl);
                record(Kind::linear_borrow_move_out, where, entity_of(entry.decl));
            }
            if (!entry.has_live_exit && !entry.has_maybe_exit) {
                continue;
            }
            // A linear sum gives one kind for each path (the sums).
            Kind kind = entry.has_live_exit ? Kind::linear_unconsumed : Kind::linear_unconsumed_on_path;
            if (entry.is_sum) {
                kind = Kind::linear_option_unconsumed;
            }
            record(kind, DECL_SOURCE_LOCATION(entry.decl), entity_of(entry.decl), entry.door_end);
        }
        for (const PendingFinding& finding : pending_) {
            if (finding.waits_for_closure_type != 0) {
                wait_for_closure(finding);
                continue;
            }
            record(finding.kind, finding.location, finding.entity, finding.door_end);
        }
    }

    // The generic closures: a line of a closure that the once door owns waits
    // for the end of the unit, where the facts of each instance of its call
    // operator are known.  The two walks of a body with a callee with no fact
    // give the same line, and the end of the unit keeps one.
    static void wait_for_closure(const PendingFinding& finding) {
        Place place = place_of(finding.location, Scope::quarantine);
        if (place.file == nullptr) {
            return;
        }
        waiting_owned_closures.push_back(WaitingOwnedClosure{finding.waits_for_closure_type, finding.fails_closed,
                                                             WalkFinding{finding.kind, place, finding.entity}});
    }

    void record(Kind kind, location_t location, const std::string& entity, DoorEnd door_end = DoorEnd::exempt) {
        if (door_end == DoorEnd::exempt && is_ended_by_door(kind)) {
            return;
        }
        Place place = place_of(location, Scope::quarantine);
        if (place.file == nullptr) {
            return;
        }
        findings_.push_back(WalkFinding{kind, place, entity});
    }
};

void add_walk_findings(const std::vector<WalkFinding>& findings) {
    for (const WalkFinding& finding : findings) {
        add_finding(finding.kind, nullptr, finding.place, finding.entity);
    }
}

// The summaries: the walk of the body of FUNCTION.  When the walk read a
// callee with no fact, and a second walk where such a callee keeps no
// reference gives other findings, the end of the unit picks the findings by
// the facts of the callees (judge_deferred_findings).  Complexity: one walk of
// the body, or two.
void analyze_body(tree function, bool is_door) {
    Analysis first(function, is_door, UnknownCallee::can_keep, WalkPurpose::findings);
    first.run();
    if (first.unknown_callees().empty()) {
        add_walk_findings(first.findings());
        return;
    }
    Analysis second(function, is_door, UnknownCallee::keeps_nothing, WalkPurpose::findings);
    second.run(&first);
    if (first.findings() == second.findings()) {
        add_walk_findings(first.findings());
        return;
    }
    deferred_findings.push_back(DeferredFindings{first.findings(), second.findings(), first.unknown_callees()});
}

// The summaries and the closures that move a capture: the capture fields of
// the closure of the call operator FUNCTION whose sum each path of its body
// leaves empty, and whether a path leaves a linear capture consumed or gives
// a capture that moves a linear capture out to the once door.  Only a closure
// with a linear capture, or with a capture of such a closure, gets the walk.
// A walk with a goto fails closed for a capture of such a closure.
// Complexity: one walk of the body for such a closure.
void note_closure_summary(tree function) {
    tree closure = DECL_CONTEXT(function);
    if (closure == NULL_TREE || !LAMBDA_TYPE_P(closure)) {
        return;
    }
    bool captures_linear = false;
    bool captures_moving_closure = false;
    for (tree field = TYPE_FIELDS(closure); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) != FIELD_DECL) {
            continue;
        }
        captures_linear = captures_linear || is_linear_capture(field);
        captures_moving_closure = captures_moving_closure || is_moving_closure_type(TREE_TYPE(field));
    }
    if (!captures_linear && !captures_moving_closure) {
        return;
    }
    Analysis walk(function, false, UnknownCallee::can_keep, WalkPurpose::summary);
    walk.run();
    std::vector<unsigned> emptied = walk.emptied_capture_fields();
    if (!emptied.empty()) {
        closure_empties[DECL_UID(function)] = std::move(emptied);
    }
    if (walk.moves_capture_out() || (captures_moving_closure && walk.is_unanalyzed())) {
        closure_moves_out.insert(DECL_UID(function));
        closure_types_move_out.insert(TYPE_UID(TYPE_MAIN_VARIANT(closure)));
    }
    // Closures that a call owns: a linear capture by value that a path
    // leaves live, or that the body never names, drops with the closure.
    closure_summarized.insert(DECL_UID(function));
    std::vector<unsigned> const ended = walk.ended_capture_fields();
    for (tree field = TYPE_FIELDS(closure); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (is_owned_linear_capture(field) && std::find(ended.begin(), ended.end(), DECL_UID(field)) == ended.end()) {
            closure_leaves_capture.insert(DECL_UID(function));
            closure_types_leave_capture.insert(TYPE_UID(TYPE_MAIN_VARIANT(closure)));
            break;
        }
    }
}

// The named bodies: the summary walk of the call operator && FUNCTION of a
// class that is no closure and that holds a linear value.  The once door owns
// such a body as it owns a closure, so the fact that a path leaves a linear
// field live serves the closures that a call owns, by the TYPE_UID of the
// class.  A walk with a goto fails closed.  Complexity: one walk of the body.
void note_named_body_summary(tree function) {
    if (!is_named_body_call(function)) {
        return;
    }
    unsigned const body = TYPE_UID(TYPE_MAIN_VARIANT(DECL_CONTEXT(function)));
    Analysis walk(function, false, UnknownCallee::can_keep, WalkPurpose::summary);
    walk.run();
    named_bodies_summarized.insert(body);
    if (walk.leaves_owned_body_live()) {
        closure_types_leave_capture.insert(body);
    }
}

// The summaries: the findings that waited for the facts of their callees.
// Each callee with no fact at the walk has its fact now, or has no body in
// the unit and keeps a reference.
void judge_deferred_findings() {
    resolve_waiting_kept_facts();
    for (const DeferredFindings& deferred : deferred_findings) {
        add_walk_findings(keeps_nothing_each(deferred.callees) ? deferred.if_not_kept : deferred.if_kept);
    }
    deferred_findings.clear();
}

// ── The shapes (refer to the section Shapes) ────────────────────────────

// The facts of the rules of the shapes, keyed by the main variant of a type.
// Each collection clears them, as it clears linear_caches.
struct ShapeCaches {
    std::unordered_map<tree, bool> refuses_lifetime;
    std::unordered_map<tree, tree> library_holder;
    std::unordered_map<tree, std::vector<tree>> brands;
    std::unordered_map<tree, std::vector<tree>> epoch_brands;
};

ShapeCaches shape_caches;

// The classes that the class rules read in this unit, by TYPE_UID, which no
// collection reuses.
std::unordered_set<int> checked_classes;

// The call edges of the bodies of this unit, and each call with a branded
// argument, by DECL_UID (the rule brand-reentry).
struct BrandedCall {
    int caller = 0;
    int callee = 0;
    location_t location = UNKNOWN_LOCATION;
    std::string entity;
};

struct CallGraph {
    std::unordered_map<int, std::vector<int>> callees;
    std::vector<BrandedCall> branded_calls;
    bool was_reported = false;
};

CallGraph call_graph;

// Records a finding of a rule of the family at LOCATION.
void record_shape(Kind kind, location_t location, const std::string& entity) {
    Place place = place_of(location, Scope::quarantine);
    if (place.file != nullptr) {
        add_finding(kind, nullptr, place, entity);
    }
}

// True when a finding about ENTITY, a declaration or a class, counts in this
// unit: in a quarantined file, in a base file only in its sentinel unit, and in
// each unit that makes an instance of a base template (the bodies).
bool is_reported_for(tree entity) {
    tree decl = TYPE_P(entity) ? TYPE_MAIN_DECL(entity) : entity;
    if (decl == NULL_TREE) {
        return false;
    }
    Place place = place_of(DECL_SOURCE_LOCATION(decl), Scope::quarantine);
    if (place.file == nullptr) {
        return false;
    }
    if (is_in_instantiation(entity)) {
        return place.file_class == FileClass::base || place.file_class == FileClass::quarantined;
    }
    return is_walked_file(place);
}

// True when TYPE carries the annotation no_start_over_bytes of
// include/foundation/ByteSeal.h, or holds a part that carries it, or holds a
// reference member: the checked lifetime start of include/foundation/Lifetime.h
// refuses it.  Complexity: the walk visits each class one time.
bool refuses_lifetime(tree type, int depth = 0) {
    type = TYPE_MAIN_VARIANT(element_type_of(type));
    if (!CLASS_TYPE_P(type) || !COMPLETE_TYPE_P(type) || depth > kTypeDepth) {
        return false;
    }
    auto found = shape_caches.refuses_lifetime.find(type);
    if (found != shape_caches.refuses_lifetime.end()) {
        return found->second;
    }
    shape_caches.refuses_lifetime.emplace(type, false);
    bool result = carries_annotation(TYPE_ATTRIBUTES(type), linear_names.no_start_over_bytes, linear_names.lifetime);
    if (tree binfo = TYPE_BINFO(type); binfo != NULL_TREE && !result) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo) && !result; ++index) {
            result = refuses_lifetime(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), depth + 1);
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE && !result; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && TREE_TYPE(field) != NULL_TREE) {
            result = TYPE_REF_P(TREE_TYPE(field)) || refuses_lifetime(TREE_TYPE(field), depth + 1);
        }
    }
    shape_caches.refuses_lifetime[type] = result;
    return result;
}

// A sealed type: a linear type, or a type that the checked lifetime start
// refuses.  No route of bytes can build one.
bool is_sealed_type(tree type) { return is_linear_type(type) || refuses_lifetime(type); }

// True when TYPE is a class of namespace std.
bool is_library_class(tree type) {
    tree decl = TYPE_MAIN_DECL(type);
    return decl != NULL_TREE && decl_in_std_namespace_p(decl);
}

// Calls VISIT for each type argument of the class TYPE, at each level and
// inside each argument pack.
template <typename Visit>
void each_type_argument(tree arguments, Visit visit, int depth = 0) {
    if (arguments == NULL_TREE || depth > kTypeDepth) {
        return;
    }
    if (TREE_CODE(arguments) != TREE_VEC) {
        if (ARGUMENT_PACK_P(arguments)) {
            each_type_argument(ARGUMENT_PACK_ARGS(arguments), visit, depth + 1);
        } else {
            visit(arguments);
        }
        return;
    }
    for (int index = 0; index < TREE_VEC_LENGTH(arguments); ++index) {
        each_type_argument(TREE_VEC_ELT(arguments, index), visit, depth + 1);
    }
}

// The class of the library that TYPE holds by value with a linear type
// argument, or null.  Such a class is no aggregate, and it is linear by its
// parts or it has a destructor that is not trivial: its operations copy or
// drop a value, and they carry no door.  A view, such as std::span, has a
// trivial destructor and holds no value.  A pointer and a reference hold no
// value.  Complexity: the walk visits each class one time.
tree library_holder_of(tree type, int depth = 0) {
    type = type != NULL_TREE && TYPE_P(type) ? TYPE_MAIN_VARIANT(element_type_of(type)) : NULL_TREE;
    if (type == NULL_TREE || !CLASS_TYPE_P(type) || LAMBDA_TYPE_P(type) || depth > kTypeDepth
        || CLASSTYPE_TEMPLATE_INFO(type) == NULL_TREE || uses_template_parms(type)) {
        return NULL_TREE;
    }
    auto found = shape_caches.library_holder.find(type);
    if (found != shape_caches.library_holder.end()) {
        return found->second;
    }
    shape_caches.library_holder.emplace(type, NULL_TREE);
    tree result = NULL_TREE;
    bool is_owner = is_library_class(type) && COMPLETE_TYPE_P(type) && !CP_AGGREGATE_TYPE_P(type)
                 && (is_linear_type(type) || TYPE_HAS_NONTRIVIAL_DESTRUCTOR(type));
    each_type_argument(CLASSTYPE_TI_ARGS(type), [&](tree argument) {
        if (result != NULL_TREE || !TYPE_P(argument)) {
            return;
        }
        if (is_owner && is_linear_type(argument)) {
            result = type;
        } else {
            result = library_holder_of(argument, depth + 1);
        }
    });
    shape_caches.library_holder[type] = result;
    return result;
}

// The macro of include/foundation/Brand.h that writes the fresh brand.
constexpr const char* kFreshBrandMacro = "CRUCIBLE_FRESH_BRAND";

// True when TYPE has the shape of a brand: the closure type of a lambda with
// no capture, no template parameter and no parameter.
bool has_brand_shape(tree type) {
    if (type == NULL_TREE || !TYPE_P(type)) {
        return false;
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!LAMBDA_TYPE_P(type)) {
        return false;
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL) {
            return false;
        }
    }
    tree call = lambda_function(type);
    return call != NULL_TREE && TREE_CODE(call) == FUNCTION_DECL && !generic_lambda_fn_p(call)
        && FUNCTION_FIRST_USER_PARMTYPE(call) == void_list_node;
}

// True when the expansion of the macro CRUCIBLE_FRESH_BRAND wrote the lambda
// of the closure TYPE.
bool is_written_by_fresh_brand(tree type) {
    tree decl = TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(type));
    location_t location = decl != NULL_TREE ? DECL_SOURCE_LOCATION(decl) : UNKNOWN_LOCATION;
    for (int depth = 0; depth < kMacroDepth && linemap_location_from_macro_expansion_p(line_table, location); ++depth) {
        const line_map* map = linemap_lookup(line_table, location);
        if (map == nullptr || !linemap_macro_expansion_map_p(map)) {
            return false;
        }
        const char* macro = linemap_map_get_macro_name(linemap_check_macro(map));
        if (macro != nullptr && std::strcmp(macro, kFreshBrandMacro) == 0) {
            return true;
        }
        location = linemap_unwind_toward_expansion(line_table, location, &map);
    }
    return false;
}

// True when TYPE is a scope brand: a closure with the shape of a brand whose
// call operator carries the annotation foundation::brand::scope_brand
// (the scope brands).
bool is_scope_brand(tree type) {
    if (!has_brand_shape(type)) {
        return false;
    }
    tree call = lambda_function(TYPE_MAIN_VARIANT(type));
    return call != NULL_TREE && carries_annotation(DECL_ATTRIBUTES(call), linear_names.scope_brand, linear_names.brand);
}

// True when TYPE is a fresh brand: a closure with the shape of a brand that
// CRUCIBLE_FRESH_BRAND of include/foundation/Brand.h writes, or a scope brand.
// A class that names a closure as its member brand_type gives a fresh brand
// too (collect_brands).  A closure with the shape of a brand that no class
// names as its brand, such as a label of a session, is no brand.
bool is_brand_closure(tree type) {
    return has_brand_shape(type) && (is_written_by_fresh_brand(type) || is_scope_brand(type));
}

// The first scope brand of BRANDS, or null.
tree first_scope_brand(const std::vector<tree>& brands) {
    for (tree brand : brands) {
        if (is_scope_brand(brand)) {
            return brand;
        }
    }
    return NULL_TREE;
}

// The closure that the class TYPE or one of its bases names as its member
// brand_type, or null.
tree named_brand_of(tree type, int depth = 0) {
    if (type == NULL_TREE || !CLASS_TYPE_P(type) || depth > kTypeDepth) {
        return NULL_TREE;
    }
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (TREE_CODE(member) == TYPE_DECL && DECL_NAME(member) == linear_names.brand_type
            && has_brand_shape(TREE_TYPE(member))) {
            return TYPE_MAIN_VARIANT(TREE_TYPE(member));
        }
    }
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            if (tree brand = named_brand_of(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), depth + 1)) {
                return brand;
            }
        }
    }
    return NULL_TREE;
}

// Adds each fresh brand that TYPE holds to OUT: a brand closure, a brand in a
// template argument or in the type of a value argument such as a function
// pointer, and a brand in a capture or a field of a closure or of a local
// class.  Complexity: the walk visits each class one time.
void collect_brands(tree type, std::vector<tree>& out, int depth = 0) {
    if (type == NULL_TREE || !TYPE_P(type) || depth > kTypeDepth) {
        return;
    }
    switch (TREE_CODE(type)) {
        case REFERENCE_TYPE:
        case POINTER_TYPE:
        case ARRAY_TYPE:
            collect_brands(TREE_TYPE(type), out, depth + 1);
            return;
        case OFFSET_TYPE:
            collect_brands(TREE_TYPE(type), out, depth + 1);
            collect_brands(TYPE_OFFSET_BASETYPE(type), out, depth + 1);
            return;
        case FUNCTION_TYPE:
        case METHOD_TYPE: {
            collect_brands(TREE_TYPE(type), out, depth + 1);
            tree parameter = TYPE_ARG_TYPES(type);
            if (TREE_CODE(type) == METHOD_TYPE && parameter != NULL_TREE) {
                parameter = TREE_CHAIN(parameter);
            }
            for (; parameter != NULL_TREE && parameter != void_list_node; parameter = TREE_CHAIN(parameter)) {
                collect_brands(TREE_VALUE(parameter), out, depth + 1);
            }
            return;
        }
        default:
            break;
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type)) {
        return;
    }
    if (is_brand_closure(type)) {
        out.push_back(type);
        return;
    }
    auto found = shape_caches.brands.find(type);
    if (found == shape_caches.brands.end()) {
        shape_caches.brands.emplace(type, std::vector<tree>{});
        std::vector<tree> held;
        if (CLASSTYPE_TEMPLATE_INFO(type) != NULL_TREE) {
            each_type_argument(CLASSTYPE_TI_ARGS(type), [&](tree argument) {
                if (TYPE_P(argument)) {
                    collect_brands(argument, held, depth + 1);
                } else if (TREE_TYPE(argument) != NULL_TREE) {
                    collect_brands(TREE_TYPE(argument), held, depth + 1);
                }
            });
        }
        if (tree named = named_brand_of(type)) {
            held.push_back(named);
        }
        tree decl = TYPE_MAIN_DECL(type);
        if (LAMBDA_TYPE_P(type) || (decl != NULL_TREE && decl_function_context(decl) != NULL_TREE)) {
            for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
                if (TREE_CODE(field) == FIELD_DECL) {
                    collect_brands(TREE_TYPE(field), held, depth + 1);
                }
            }
        }
        found = shape_caches.brands.find(type);
        found->second = std::move(held);
    }
    out.insert(out.end(), found->second.begin(), found->second.end());
}

// True when BRAND is held by a template argument of SCOPE or of a class or a
// function that holds SCOPE: the brand came in from outside, so SCOPE did not
// make it (the brands).
bool is_bound_brand(tree brand, tree scope) {
    for (int depth = 0; scope != NULL_TREE && depth < kTypeDepth; ++depth) {
        tree info = NULL_TREE;
        tree next = NULL_TREE;
        if (TYPE_P(scope)) {
            scope = TYPE_MAIN_VARIANT(scope);
            info = CLASS_TYPE_P(scope) ? CLASSTYPE_TEMPLATE_INFO(scope) : NULL_TREE;
            next = CP_TYPE_CONTEXT(scope);
        } else if (TREE_CODE(scope) == FUNCTION_DECL) {
            info = DECL_LANG_SPECIFIC(scope) != nullptr ? DECL_TEMPLATE_INFO(scope) : NULL_TREE;
            next = CP_DECL_CONTEXT(scope);
        } else if (TREE_CODE(scope) == NAMESPACE_DECL) {
            return false;
        } else if (DECL_P(scope)) {
            next = CP_DECL_CONTEXT(scope);
        } else {
            return false;
        }
        if (info != NULL_TREE && TI_ARGS(info) != NULL_TREE) {
            bool is_held = false;
            each_type_argument(TI_ARGS(info), [&](tree argument) {
                std::vector<tree> held;
                if (TYPE_P(argument) && TYPE_MAIN_VARIANT(argument) == brand) {
                    held.push_back(brand);
                } else if (TYPE_P(argument)) {
                    collect_brands(argument, held);
                } else if (TREE_TYPE(argument) != NULL_TREE && has_brand_shape(TREE_TYPE(argument))) {
                    held.push_back(TYPE_MAIN_VARIANT(TREE_TYPE(argument)));
                } else if (TREE_TYPE(argument) != NULL_TREE) {
                    collect_brands(TREE_TYPE(argument), held);
                }
                is_held = is_held || std::find(held.begin(), held.end(), brand) != held.end();
            });
            if (is_held) {
                return true;
            }
        }
        scope = next;
    }
    return false;
}

// The first brand of TYPE that SCOPE did not get from outside, or null.
tree unbound_brand(tree type, tree scope) {
    std::vector<tree> held;
    collect_brands(type, held);
    for (tree brand : held) {
        if (!is_bound_brand(brand, scope)) {
            return brand;
        }
    }
    return NULL_TREE;
}

// ── The arena brands ───────────────────────────────────────────────────────

// True when the class TYPE carries the annotation foundation::brand::NAME, or
// the pattern of its class template does.  A brand in a template argument is
// often never instantiated, and such a specialization holds no attribute.
bool carries_brand_mark(tree type, tree name) {
    if (type == NULL_TREE || !TYPE_P(type)) {
        return false;
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type)) {
        return false;
    }
    if (carries_annotation(TYPE_ATTRIBUTES(type), name, linear_names.brand)) {
        return true;
    }
    tree info = CLASSTYPE_TEMPLATE_INFO(type);
    if (info == NULL_TREE || TREE_CODE(TI_TEMPLATE(info)) != TEMPLATE_DECL) {
        return false;
    }
    tree pattern = TREE_TYPE(DECL_TEMPLATE_RESULT(most_general_template(TI_TEMPLATE(info))));
    return pattern != NULL_TREE && pattern != type && CLASS_TYPE_P(pattern)
        && carries_annotation(TYPE_ATTRIBUTES(pattern), name, linear_names.brand);
}

// True when TYPE is an epoch brand: a class with the annotation epoch_brand,
// the class template EpochOf of include/foundation/core/BumpArena.h.
bool is_epoch_brand(tree type) { return carries_brand_mark(type, linear_names.epoch_brand); }

// True when the class TYPE states a borrow of include/foundation/Transfer.h,
// such as a Ref or a View.
bool is_borrow_class(tree type) {
    tree attributes = TYPE_ATTRIBUTES(TYPE_MAIN_VARIANT(type));
    return carries_annotation(attributes, linear_names.refers, linear_names.transfer)
        || carries_annotation(attributes, linear_names.refers_unique, linear_names.transfer)
        || carries_annotation(attributes, linear_names.refers_deep, linear_names.transfer);
}

// The member tag of TYPE when TYPE is an owner of a member tag: a class with
// the annotation branded_arena with a type argument that carries
// arena_member_brand, at any position, such as the first argument of an
// arena and the brand of a SlotMap.  The first such argument is the tag.
// Null for each other type.  Complexity: linear in the type arguments.
tree member_arena_tag(tree type) {
    if (!carries_brand_mark(type, linear_names.branded_arena)) {
        return NULL_TREE;
    }
    tree info = CLASSTYPE_TEMPLATE_INFO(TYPE_MAIN_VARIANT(type));
    tree arguments = info != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)) : NULL_TREE;
    for (int index = 0; arguments != NULL_TREE && index < TREE_VEC_LENGTH(arguments); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (argument != NULL_TREE && TYPE_P(argument)
            && carries_brand_mark(argument, linear_names.arena_member_brand)) {
            return TYPE_MAIN_VARIANT(argument);
        }
    }
    return NULL_TREE;
}

// The first arena of a member tag that a value of TYPE holds as an owner:
// TYPE itself, the element of an array, and a type argument of a class that
// states no borrow, at each depth, such as the payload of an Option or of a
// Box.  A reference, a pointer and a borrow name an arena that a different
// object holds.  Complexity: linear in the type arguments, to kTypeDepth.
tree held_member_arena(tree type, int depth = 0) {
    if (type == NULL_TREE || !TYPE_P(type) || depth > kTypeDepth) {
        return NULL_TREE;
    }
    type = element_type_of(type);
    if (!CLASS_TYPE_P(type)) {
        return NULL_TREE;
    }
    type = TYPE_MAIN_VARIANT(type);
    if (member_arena_tag(type) != NULL_TREE) {
        return type;
    }
    if (is_borrow_class(type) || CLASSTYPE_TEMPLATE_INFO(type) == NULL_TREE) {
        return NULL_TREE;
    }
    tree found = NULL_TREE;
    each_type_argument(CLASSTYPE_TI_ARGS(type), [&](tree argument) {
        if (found == NULL_TREE && TYPE_P(argument)) {
            found = held_member_arena(argument, depth + 1);
        }
    });
    return found;
}

// The epoch brand that the class TYPE or one of its bases names as its member
// brand_type: TYPE is a carrier of that epoch.  Null for each other class.
tree named_epoch_brand_of(tree type, int depth = 0) {
    if (type == NULL_TREE || !CLASS_TYPE_P(type) || depth > kTypeDepth) {
        return NULL_TREE;
    }
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (TREE_CODE(member) == TYPE_DECL && DECL_NAME(member) == linear_names.brand_type
            && is_epoch_brand(TREE_TYPE(member))) {
            return TYPE_MAIN_VARIANT(TREE_TYPE(member));
        }
    }
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            if (tree brand = named_epoch_brand_of(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), depth + 1)) {
                return brand;
            }
        }
    }
    return NULL_TREE;
}

// Adds each epoch brand that TYPE holds to OUT: an epoch brand, a brand in a
// type argument at each depth, through a reference, a pointer and an array,
// the brand that a carrier names, and a brand in a capture or a field of a
// closure or of a local class.  Complexity: the walk visits each class one
// time between two collections.
void collect_epoch_brands(tree type, std::vector<tree>& out, int depth = 0) {
    if (type == NULL_TREE || !TYPE_P(type) || depth > kTypeDepth) {
        return;
    }
    if (TREE_CODE(type) == REFERENCE_TYPE || TREE_CODE(type) == POINTER_TYPE || TREE_CODE(type) == ARRAY_TYPE) {
        collect_epoch_brands(TREE_TYPE(type), out, depth + 1);
        return;
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type)) {
        return;
    }
    if (is_epoch_brand(type)) {
        out.push_back(type);
        return;
    }
    auto found = shape_caches.epoch_brands.find(type);
    if (found == shape_caches.epoch_brands.end()) {
        shape_caches.epoch_brands.emplace(type, std::vector<tree>{});
        std::vector<tree> held;
        if (CLASSTYPE_TEMPLATE_INFO(type) != NULL_TREE) {
            each_type_argument(CLASSTYPE_TI_ARGS(type), [&](tree argument) {
                if (TYPE_P(argument)) {
                    collect_epoch_brands(argument, held, depth + 1);
                }
            });
        }
        if (tree named = named_epoch_brand_of(type)) {
            held.push_back(named);
        }
        tree decl = TYPE_MAIN_DECL(type);
        if (LAMBDA_TYPE_P(type) || (decl != NULL_TREE && decl_function_context(decl) != NULL_TREE)) {
            for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
                if (TREE_CODE(field) == FIELD_DECL) {
                    collect_epoch_brands(TREE_TYPE(field), held, depth + 1);
                }
            }
        }
        found = shape_caches.epoch_brands.find(type);
        found->second = std::move(held);
    }
    out.insert(out.end(), found->second.begin(), found->second.end());
}

// The first arena of a member tag that a value of TYPE reaches in a place
// that is not const: TYPE itself, a base, a member, a type argument, and the
// target of a reference or a pointer, at each depth.  A const arena gives
// nothing and opens no epoch.  C++ const is shallow, so IN_CONST, the const of
// the place, goes on only to the parts that the place holds by value, and not
// to a mutable member.  The target of a pointer, of a reference and of a
// borrow, such as a Ref, keeps its own const.  A body with no capture reaches
// each object of static or thread storage duration, so such an object that
// reaches an epoch arena would let a body open a second epoch of it.
// Complexity: each pair of a class and its const one time for each walk.
tree reached_member_arena(tree type, std::set<std::pair<tree, bool>>& seen, bool in_const, int depth = 0) {
    if (type == NULL_TREE || !TYPE_P(type) || depth > kTypeDepth) {
        return NULL_TREE;
    }
    if (TREE_CODE(type) == REFERENCE_TYPE || TREE_CODE(type) == POINTER_TYPE) {
        return reached_member_arena(TREE_TYPE(type), seen, TYPE_READONLY(TREE_TYPE(type)), depth + 1);
    }
    if (TREE_CODE(type) == ARRAY_TYPE) {
        return reached_member_arena(TREE_TYPE(type), seen, in_const || TYPE_READONLY(type), depth + 1);
    }
    bool const is_const = in_const || TYPE_READONLY(type);
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type) || !seen.insert({type, is_const}).second) {
        return NULL_TREE;
    }
    if (member_arena_tag(type) != NULL_TREE) {
        return is_const ? NULL_TREE : type;
    }
    // A class holds what its fields show, so the walk reads the type arguments
    // only of a class that states that it holds their values in its own bytes
    // (by_arguments{} or payload_cell{} of include/foundation/Transfer.h).  An
    // argument of each other class can be a phantom type, such as the owner of
    // a context, of which the class holds no value.
    tree found = NULL_TREE;
    tree attributes = TYPE_ATTRIBUTES(type);
    bool const holds_arguments = carries_annotation(attributes, linear_names.by_arguments, linear_names.transfer)
                              || carries_annotation(attributes, linear_names.payload_cell, linear_names.transfer);
    if (holds_arguments && CLASSTYPE_TEMPLATE_INFO(type) != NULL_TREE) {
        each_type_argument(CLASSTYPE_TI_ARGS(type), [&](tree argument) {
            if (found == NULL_TREE && TYPE_P(argument)) {
                found = reached_member_arena(argument, seen, is_const || TYPE_READONLY(argument), depth + 1);
            }
        });
    }
    if (!COMPLETE_TYPE_P(type)) {
        return found;
    }
    for (tree field = TYPE_FIELDS(type); found == NULL_TREE && field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && TREE_TYPE(field) != NULL_TREE) {
            bool const field_const = is_const && !DECL_MUTABLE_P(field);
            found = reached_member_arena(TREE_TYPE(field), seen, field_const, depth + 1);
        }
    }
    return found;
}

// The first epoch brand that TYPE holds, or null.
tree first_epoch_brand(tree type) {
    std::vector<tree> held;
    collect_epoch_brands(type, held);
    return held.empty() ? NULL_TREE : held.front();
}

// The name of a class in an entity.
std::string class_text(tree type) { return qualified_name(TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(type))); }

// The checks of the objects with static or thread storage duration and of the
// locals (the arena brands): an arena of a member tag lives only in the one
// member that its tag names, and an epoch brand lives in no such object.
void check_arena_object(tree decl, const std::string& entity, location_t location, bool is_static) {
    tree type = TREE_TYPE(decl);
    if (tree arena = held_member_arena(type)) {
        record_shape(Kind::brand_arena_member, location,
                     entity + ", an object of the arena " + class_text(arena) + " that is no member of the class of "
                         + class_text(member_arena_tag(arena)));
    }
    if (!is_static) {
        return;
    }
    std::set<std::pair<tree, bool>> seen;
    tree reached = held_member_arena(type) == NULL_TREE ? reached_member_arena(type, seen, false) : NULL_TREE;
    if (reached != NULL_TREE) {
        record_shape(Kind::brand_arena_member, location,
                     entity + ", an object of static or thread storage duration that reaches the arena "
                         + class_text(reached) + " of the class of " + class_text(member_arena_tag(reached)));
    }
    if (tree epoch = first_epoch_brand(type)) {
        record_shape(Kind::brand_epoch_static, location, entity + ", " + class_text(epoch));
    }
}

// The checks of the members of the complete class TYPE (the arena brands).
// A member that holds an arena of a member tag is that arena itself, its tag
// is a nested class of TYPE, and no second member holds an arena of the same
// tag.  Each nested member tag of TYPE names one member.  A member that holds
// an epoch brand is a member of a carrier of that brand.  Complexity: linear
// in the members, times the walk of each type.
void check_member_brands(tree type, const std::string& name) {
    std::vector<tree> named_tags;
    tree carried = named_epoch_brand_of(type);
    // A class template whose type argument holds the arena, such as an Option
    // of it, is the different type of the finding at the member that holds it.
    bool const is_wrapper = CLASSTYPE_TEMPLATE_INFO(type) != NULL_TREE && held_member_arena(type) != NULL_TREE;
    // An epoch brand that a type argument of the class gives is bound, as the
    // payload of an Option is: the member that holds the class gives the
    // finding.
    std::vector<tree> bound;
    if (CLASSTYPE_TEMPLATE_INFO(type) != NULL_TREE) {
        each_type_argument(CLASSTYPE_TI_ARGS(type), [&](tree argument) {
            if (TYPE_P(argument)) {
                collect_epoch_brands(argument, bound);
            }
        });
    }
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (TREE_CODE(member) != FIELD_DECL || DECL_FIELD_IS_BASE(member) || TREE_TYPE(member) == NULL_TREE
            || TREE_TYPE(member) == error_mark_node) {
            continue;
        }
        std::string entity = name + ": " + simple_name(member);
        location_t location = DECL_SOURCE_LOCATION(member);
        std::vector<tree> epochs;
        collect_epoch_brands(TREE_TYPE(member), epochs);
        for (tree epoch : epochs) {
            if (epoch != carried && std::find(bound.begin(), bound.end(), epoch) == bound.end()) {
                record_shape(Kind::brand_epoch_member, location, entity + ", " + class_text(epoch));
                break;
            }
        }
        tree arena = held_member_arena(TREE_TYPE(member));
        if (arena == NULL_TREE || is_wrapper) {
            continue;
        }
        tree tag = member_arena_tag(arena);
        std::string tag_name = class_text(tag);
        if (TYPE_MAIN_VARIANT(strip_top_quals(TREE_TYPE(member))) != arena) {
            record_shape(Kind::brand_arena_member, location,
                         entity + ", the arena of the member tag " + tag_name + " inside a different type");
            named_tags.push_back(tag);
        } else if (TYPE_CONTEXT(tag) != type) {
            record_shape(Kind::brand_arena_member, location,
                         entity + ", the member tag " + tag_name + " is no nested class of the class");
        } else if (std::find(named_tags.begin(), named_tags.end(), tag) != named_tags.end()) {
            record_shape(Kind::brand_arena_member, location,
                         entity + ", a second member of the member tag " + tag_name);
        } else {
            named_tags.push_back(tag);
        }
    }
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (TREE_CODE(member) != TYPE_DECL || !DECL_IMPLICIT_TYPEDEF_P(member)) {
            continue;
        }
        tree tag = TYPE_MAIN_VARIANT(TREE_TYPE(member));
        if (carries_brand_mark(tag, linear_names.arena_member_brand) && TYPE_CONTEXT(tag) == type
            && std::find(named_tags.begin(), named_tags.end(), tag) == named_tags.end()) {
            record_shape(Kind::brand_arena_member, DECL_SOURCE_LOCATION(member),
                         name + ": the member tag " + class_text(tag) + " names no member");
        }
    }
}

// The name of a brand in an entity: the place of its lambda, because GCC
// prints each brand as <lambda()>.
std::string brand_text(tree brand) {
    tree decl = TYPE_MAIN_DECL(brand);
    Place place = decl != NULL_TREE ? place_of(DECL_SOURCE_LOCATION(decl), Scope::tree) : Place{};
    if (place.file == nullptr) {
        return "a brand of a library";
    }
    return "the brand of " + place.file->relative + ":" + std::to_string(place.line);
}

// True when the end of the scope of a value of TYPE is a door: the class
// carries scope_end_door, or its destructor is a door.
bool has_scope_door(tree type);

// True when the class TYPE has a user-provided copy constructor or copy
// assignment that is not deleted.
bool has_user_copy(tree type) {
    // A copy whose constraints an instance does not satisfy is no copy of it.
    auto is_user_copy = [](tree function) {
        return TREE_CODE(function) == FUNCTION_DECL && copy_fn_p(function) > 0 && !DECL_DELETED_FN(function)
            && user_provided_p(function) && constraints_satisfied_p(function);
    };
    for (ovl_iterator constructor(CLASSTYPE_CONSTRUCTORS(type)); constructor; ++constructor) {
        if (is_user_copy(*constructor)) {
            return true;
        }
    }
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (TREE_CODE(member) == FUNCTION_DECL && DECL_ASSIGNMENT_OPERATOR_P(member) && is_user_copy(member)) {
            return true;
        }
    }
    return false;
}

// True when TYPE holds a linear base or a linear field by value.
bool holds_linear_part(tree type) {
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            if (is_linear_type(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)))) {
                return true;
            }
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && !DECL_FIELD_IS_BASE(field) && TREE_TYPE(field) != NULL_TREE
            && !TYPE_REF_P(TREE_TYPE(field)) && is_linear_type(TREE_TYPE(field))) {
            return true;
        }
    }
    return false;
}

void check_class(tree type, int depth = 0);

// The checks of an object with static or thread storage duration: a brand in
// it escapes each run of its mint site, and a class of the library with a
// linear argument drops its values.  A scope brand gives the kind
// scope-static, which is an error in each mode (the scope brands).
void check_static_object(tree decl, const std::string& owner) {
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE || type == error_mark_node || uses_template_parms(type)) {
        return;
    }
    std::string entity = owner + ": " + simple_name(decl);
    std::vector<tree> held;
    collect_brands(type, held);
    if (tree scope = first_scope_brand(held)) {
        record_shape(Kind::brand_scope_static, DECL_SOURCE_LOCATION(decl), entity + ", " + brand_text(scope));
    } else if (!held.empty()) {
        record_shape(Kind::brand_escaped_static, DECL_SOURCE_LOCATION(decl), entity + ", " + brand_text(held.front()));
    }
    if (tree holder = library_holder_of(type)) {
        record_shape(Kind::linear_library_holder, DECL_SOURCE_LOCATION(decl),
                     entity + ", " + qualified_name(TYPE_MAIN_DECL(holder)));
    }
    check_arena_object(decl, entity, DECL_SOURCE_LOCATION(decl), true);
    check_class(type);
}

// An alias outside a body: a brand in its type escapes each run of its mint
// site, unless a template argument of its class gave the brand.
void check_alias(tree decl, tree scope, const std::string& owner) {
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE || type == error_mark_node || uses_template_parms(type)) {
        return;
    }
    if (tree brand = unbound_brand(type, scope)) {
        record_shape(Kind::brand_escaped_alias, DECL_SOURCE_LOCATION(decl),
                     owner + ": " + simple_name(decl) + ", " + brand_text(brand));
    }
}

// The class rules: linear-containment, linear-forge and the members of
// brand-escape, for one complete class that is no pattern (the shapes).  The
// walk goes on into the class of each base and each field.  Complexity: each
// class one time in each unit.
void check_class(tree type, int depth) {
    type = type != NULL_TREE && TYPE_P(type) ? TYPE_MAIN_VARIANT(element_type_of(type)) : NULL_TREE;
    if (type == NULL_TREE || !CLASS_TYPE_P(type) || !COMPLETE_TYPE_P(type) || LAMBDA_TYPE_P(type) || depth > kTypeDepth
        || uses_template_parms(type) || !checked_classes.insert(TYPE_UID(type)).second) {
        return;
    }
    tree decl = TYPE_MAIN_DECL(type);
    if (decl == NULL_TREE || in_library_file_at(DECL_SOURCE_LOCATION(decl))) {
        return;
    }
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            check_class(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), depth + 1);
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && TREE_TYPE(field) != NULL_TREE) {
            check_class(TREE_TYPE(field), depth + 1);
        }
    }
    if (!is_reported_for(type)) {
        return;
    }
    location_t location = DECL_SOURCE_LOCATION(decl);
    std::string name = qualified_name(decl);
    TypeFacts facts = facts_of(type);
    // A union ends no member at its end, and its owner chooses the active
    // member: the rule linear-forge reads it.
    if (TREE_CODE(type) != UNION_TYPE && holds_linear_part(type)) {
        if (facts.linearity == Linearity::none && !has_scope_door(type)) {
            record_shape(Kind::linear_pinned_holder, location, name);
        }
        if (has_user_copy(type)) {
            record_shape(Kind::linear_copyable_holder, location, name);
        }
    }
    if (facts.linearity == Linearity::direct && !refuses_lifetime(type)) {
        record_shape(Kind::linear_unsealed_lifetime, location, name);
    }
    check_member_brands(type, name);
    bool is_union = TREE_CODE(type) == UNION_TYPE;
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        tree member_type = TREE_TYPE(member);
        if (member_type == NULL_TREE || member_type == error_mark_node) {
            continue;
        }
        if (TREE_CODE(member) == FIELD_DECL && !DECL_FIELD_IS_BASE(member)) {
            std::string entity = name + ": " + simple_name(member);
            if (is_union && is_sealed_type(member_type)) {
                record_shape(Kind::linear_sealed_union, DECL_SOURCE_LOCATION(member), entity);
            }
            if (tree holder = library_holder_of(member_type)) {
                record_shape(Kind::linear_library_holder, DECL_SOURCE_LOCATION(member),
                             entity + ", " + qualified_name(TYPE_MAIN_DECL(holder)));
            }
            if (tree brand = unbound_brand(member_type, type)) {
                record_shape(Kind::brand_escaped_member, DECL_SOURCE_LOCATION(member),
                             entity + ", " + brand_text(brand));
            }
        } else if (VAR_P(member)) {
            check_static_object(member, name);
        } else if (TREE_CODE(member) == TYPE_DECL && !DECL_ARTIFICIAL(member) && !DECL_IMPLICIT_TYPEDEF_P(member)
                   && !DECL_SELF_REFERENCE_P(member)) {
            check_alias(member, type, name);
        }
    }
}

// The brands that the instance FUNCTION mints: the argument of each parameter
// of its innermost template parameter list whose default is a closure.
// Complexity: linear in the template parameters.
std::vector<tree> fresh_arguments_of(tree function) {
    std::vector<tree> fresh;
    if (DECL_LANG_SPECIFIC(function) == nullptr || DECL_TEMPLATE_INFO(function) == NULL_TREE) {
        return fresh;
    }
    tree template_decl = DECL_TI_TEMPLATE(function);
    if (template_decl == NULL_TREE || TREE_CODE(template_decl) != TEMPLATE_DECL) {
        return fresh;
    }
    tree parameters = innermost_parameters_of(template_decl);
    tree arguments = DECL_TI_ARGS(function) != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(DECL_TI_ARGS(function)) : NULL_TREE;
    if (parameters == NULL_TREE || arguments == NULL_TREE || TREE_CODE(arguments) != TREE_VEC) {
        return fresh;
    }
    int count = std::min(TREE_VEC_LENGTH(parameters), TREE_VEC_LENGTH(arguments));
    for (int index = 0; index < count; ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        tree default_argument = TREE_PURPOSE(TREE_VEC_ELT(parameters, index));
        if (argument != NULL_TREE && TYPE_P(argument) && is_closure_default(default_argument)) {
            fresh.push_back(TYPE_MAIN_VARIANT(argument));
        }
    }
    return fresh;
}

// The checks of the signature of a function whose body the unit reads: a
// return value and a parameter of a class of the library with a linear
// argument, a fresh brand that the function made and returns, and a scope
// brand that the function mints and returns (the scope brands).
void check_signature(tree function) {
    std::string name = function_text(function);
    location_t location = DECL_SOURCE_LOCATION(function);
    tree result = TREE_TYPE(TREE_TYPE(function));
    if (!DECL_CONSTRUCTOR_P(function) && !DECL_DESTRUCTOR_P(function) && result != NULL_TREE
        && !uses_template_parms(result)) {
        if (tree holder = library_holder_of(result)) {
            record_shape(Kind::linear_library_holder, location,
                         name + ": the return value, " + qualified_name(TYPE_MAIN_DECL(holder)));
        }
        if (tree brand = unbound_brand(result, function)) {
            record_shape(Kind::brand_escaped_return, location, name + ": " + brand_text(brand));
        }
        std::vector<tree> held;
        collect_brands(result, held);
        for (tree fresh : fresh_arguments_of(function)) {
            if (is_scope_brand(fresh) && std::find(held.begin(), held.end(), fresh) != held.end()) {
                record_shape(Kind::brand_scope_return, location,
                             name + ": the result " + type_text(result) + ", " + brand_text(fresh));
            }
        }
        check_class(result);
    }
    for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        tree type = TREE_TYPE(parameter);
        if (DECL_ARTIFICIAL(parameter) || type == NULL_TREE || uses_template_parms(type)) {
            continue;
        }
        check_class(type);
        if (tree holder = library_holder_of(type)) {
            record_shape(Kind::linear_library_holder, DECL_SOURCE_LOCATION(parameter),
                         name + ": " + simple_name(parameter) + ", " + qualified_name(TYPE_MAIN_DECL(holder)));
        }
    }
}

// The walk of one body for the rules of the shapes.  It also tells whether
// the body holds a linear object or a linear value, so that must-consume runs
// only on such a body.
struct ShapeWalk {
    tree function = NULL_TREE;
    std::string name;
    // A door, a transfer and a destructor end or move the linear values of
    // their frame by their contract, so drop-door reads none of them.
    bool is_exempt_from_drops = false;
    bool has_linear = false;
    // The location of the last node with a location, for a node that the
    // front end builds with none, such as the clobber of a trivial destructor.
    location_t last_location = UNKNOWN_LOCATION;
    // The locals of the body, and each local that a call of the same type
    // initializes (check_local).
    std::vector<tree> locals;
    std::unordered_set<tree> from_call;
    hash_set<tree> visited;

    location_t location_of(tree node) const {
        if (node != NULL_TREE && EXPR_P(node) && EXPR_HAS_LOCATION(node)) {
            return EXPR_LOCATION(node);
        }
        return last_location != UNKNOWN_LOCATION ? last_location : DECL_SOURCE_LOCATION(function);
    }
};

// The call or the aggregate initialization that INITIAL is, through a
// temporary and its casts, or null.
tree initializing_call(tree initial) {
    for (int depth = 0; initial != NULL_TREE && depth < kTypeDepth; ++depth) {
        initial = tree_strip_any_location_wrapper(initial);
        if (TREE_CODE(initial) == TARGET_EXPR) {
            initial = TARGET_EXPR_INITIAL(initial);
        } else if (TREE_CODE(initial) == NOP_EXPR || TREE_CODE(initial) == CLEANUP_POINT_EXPR) {
            initial = TREE_OPERAND(initial, 0);
        } else {
            break;
        }
    }
    if (initial == NULL_TREE || (TREE_CODE(initial) != CALL_EXPR && TREE_CODE(initial) != AGGR_INIT_EXPR)) {
        return NULL_TREE;
    }
    tree callee = cp_get_callee_fndecl_nofold(initial);
    if (callee != NULL_TREE && TREE_CODE(callee) == FUNCTION_DECL && DECL_CONSTRUCTOR_P(callee)) {
        return NULL_TREE;
    }
    return initial;
}

// True when INITIAL is a call that gives a value of TYPE: the local then takes
// the type of the callee (check_local).
bool is_call_of_type(tree initial, tree type) {
    tree call = initializing_call(initial);
    if (call == NULL_TREE || type == NULL_TREE) {
        return false;
    }
    // An AGGR_INIT_EXPR has the type void, and its callee gives the type.
    tree callee = cp_get_callee_fndecl_nofold(call);
    tree result =
        callee != NULL_TREE && TREE_CODE(callee) == FUNCTION_DECL ? TREE_TYPE(TREE_TYPE(callee)) : TREE_TYPE(call);
    return result != NULL_TREE && TYPE_MAIN_VARIANT(result) == TYPE_MAIN_VARIANT(type);
}

// How a node names an object for is_frame_object: as the object itself, as
// the target of an assignment does, or as its address, as the object argument
// of a destructor or an assignment operator does.
enum class ObjectForm : std::uint8_t {
    object,
    address,
};

// The tracked object of must-consume that NODE names: a local, a parameter
// by value or by rvalue reference, or a structured binding.  An assignment to
// it is the business of must-consume.  When NODE is an address (FORM), the
// value of a local or a parameter of a pointer type names its pointee, which
// is no tracked object, so p->~T() and a delete of p end an object behind a
// pointer.  The address of such a local names the pointer itself.
bool is_frame_object(tree node, ObjectForm form) {
    // True when a declaration that the walk reaches is the object itself, and
    // not a pointer whose pointee is the object.
    bool is_object_itself = form == ObjectForm::object;
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        switch (TREE_CODE(node)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case VIEW_CONVERT_EXPR:
                node = TREE_OPERAND(node, 0);
                continue;
            case ADDR_EXPR:
                is_object_itself = true;
                node = TREE_OPERAND(node, 0);
                continue;
            case INDIRECT_REF: {
                tree pointer = tree_strip_any_location_wrapper(TREE_OPERAND(node, 0));
                return TREE_CODE(pointer) == PARM_DECL && TYPE_REF_P(TREE_TYPE(pointer))
                    && TYPE_REF_IS_RVALUE(TREE_TYPE(pointer));
            }
            case TARGET_EXPR:
                return true;
            case VAR_DECL:
                return !TREE_STATIC(node) && !DECL_EXTERNAL(node) && (is_object_itself || !TYPE_PTR_P(TREE_TYPE(node)))
                    && (!TYPE_REF_P(TREE_TYPE(node)) || DECL_DECOMPOSITION_P(node));
            case PARM_DECL:
                return !TYPE_REF_P(TREE_TYPE(node)) && (is_object_itself || !TYPE_PTR_P(TREE_TYPE(node)));
            default:
                return false;
        }
    }
    return false;
}

// True when the class TYPE has a door that consumes a const object: a member
// function with the qualifier const&& that is not deleted, or a mutable
// field.  The walk reads the class, each base and each member that it holds
// by value, to the depth kTypeDepth (the captures).  Complexity: linear in
// the members of the classes that it reads.
bool consumes_through_const(tree type, int depth) {
    tree main = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(main) || depth >= kTypeDepth) {
        return false;
    }
    for (tree member = TYPE_FIELDS(main); member != NULL_TREE; member = DECL_CHAIN(member)) {
        tree function = TREE_CODE(member) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(member) : member;
        if (TREE_CODE(function) == FUNCTION_DECL) {
            tree method = TREE_TYPE(function);
            bool const is_deleted = DECL_LANG_SPECIFIC(function) != nullptr && DECL_DELETED_FN(function);
            if (DECL_IOBJ_MEMBER_FUNCTION_P(function) && !is_deleted && FUNCTION_RVALUE_QUALIFIED(method)
                && (type_memfn_quals(method) & TYPE_QUAL_CONST) != 0) {
                return true;
            }
            continue;
        }
        if (TREE_CODE(member) != FIELD_DECL) {
            continue;
        }
        if (DECL_MUTABLE_P(member)) {
            return true;
        }
        tree held = TREE_TYPE(member);
        if (!TYPE_REF_P(held) && consumes_through_const(strip_array_types(held), depth + 1)) {
            return true;
        }
    }
    return false;
}

// True when FIELD of the class CLOSURE is a capture by reference of a linear
// object that is no loan key, and that the closure can consume.  A loan key
// is a linear class with the annotation loan_key_mark of
// foundation/core/Frame.h, and the end of its scope is its door, so the
// closure hides no consume of it.  A capture by const reference of a class
// with no door that consumes a const object only reads it (the captures).
bool is_linear_reference_capture(tree closure, tree field) {
    static tree const loan_key_mark = get_identifier("loan_key_mark");
    static tree const core = get_identifier("core");
    if (closure == NULL_TREE || !LAMBDA_TYPE_P(TYPE_MAIN_VARIANT(closure)) || field == NULL_TREE
        || TREE_CODE(field) != FIELD_DECL || !TYPE_REF_P(TREE_TYPE(field))) {
        return false;
    }
    tree object = TREE_TYPE(TREE_TYPE(field));
    if (!is_linear_type(object)) {
        return false;
    }
    tree main = TYPE_MAIN_VARIANT(object);
    bool const is_loan_key = CLASS_TYPE_P(main) && carries_base_annotation(TYPE_ATTRIBUTES(main), loan_key_mark, core);
    bool const only_reads = TYPE_READONLY(object) && !consumes_through_const(main, 0);
    return !is_loan_key && !only_reads;
}

// One capture by reference of a linear object, the field FIELD of the closure
// CLOSURE that VALUE initializes (the rule linear-capture).
void record_capture(tree closure, tree field, tree value, const ShapeWalk& walk) {
    tree lambda = CLASSTYPE_LAMBDA_EXPR(TYPE_MAIN_VARIANT(closure));
    location_t fallback = lambda != NULL_TREE ? LAMBDA_EXPR_LOCATION(lambda) : DECL_SOURCE_LOCATION(walk.function);
    tree captured = value;
    for (int depth = 0; captured != NULL_TREE && depth < kTypeDepth && !DECL_P(captured); ++depth) {
        captured = tree_strip_any_location_wrapper(captured);
        if (!EXPR_P(captured) || TREE_OPERAND_LENGTH(captured) == 0) {
            break;
        }
        captured = TREE_OPERAND(captured, 0);
    }
    std::string object = captured != NULL_TREE && DECL_P(captured) ? simple_name(captured) : simple_name(field);
    location_t location =
        value != NULL_TREE && EXPR_P(value) && EXPR_HAS_LOCATION(value) ? EXPR_LOCATION(value) : fallback;
    record_shape(Kind::linear_reference_capture, location, walk.name + ": " + object);
}

// The captures of a closure that a braced list builds.
void check_captures(tree constructor, const ShapeWalk& walk) {
    tree closure = TREE_TYPE(constructor);
    vec<constructor_elt, va_gc>* elements = CONSTRUCTOR_ELTS(constructor);
    for (unsigned index = 0; index < vec_safe_length(elements); ++index) {
        if (is_linear_reference_capture(closure, (*elements)[index].index)) {
            record_capture(closure, (*elements)[index].index, (*elements)[index].value, walk);
        }
    }
}

// True when TARGET names a parameter of FUNCTION that must-consume tracks as
// a lent object: a reference that is not const to a linear class, and no
// explicit object parameter (the lent parameters).
bool is_lent_parameter_target(tree target, tree function) {
    for (int depth = 0; target != NULL_TREE && depth < kTypeDepth; ++depth) {
        target = tree_strip_any_location_wrapper(target);
        tree_code code = TREE_CODE(target);
        if (code != ADDR_EXPR && code != NOP_EXPR && code != CONVERT_EXPR && code != INDIRECT_REF) {
            break;
        }
        target = TREE_OPERAND(target, 0);
    }
    if (target == NULL_TREE || TREE_CODE(target) != PARM_DECL) {
        return false;
    }
    if (target == DECL_ARGUMENTS(function) && DECL_XOBJ_MEMBER_FUNCTION_P(function)) {
        return false;
    }
    tree type = TREE_TYPE(target);
    return type != NULL_TREE && TYPE_REF_P(type) && !TYPE_REF_IS_RVALUE(type) && !TYPE_READONLY(TREE_TYPE(type))
        && is_linear_type(TREE_TYPE(type));
}

// The rule drop-door at an assignment: an assignment over a linear object
// that is no tracked object of must-consume drops its old value.  A lent
// parameter is such a tracked object when must-consume runs in the unit.
// FORM tells whether TARGET is the object or its address (is_frame_object).
void check_assignment_target(tree target, ObjectForm form, location_t location, const ShapeWalk& walk) {
    if (walk.is_exempt_from_drops || target == NULL_TREE || is_frame_object(target, form)) {
        return;
    }
    if (linear_mode.checks_must_consume && is_lent_parameter_target(target, walk.function)) {
        return;
    }
    tree type = TREE_TYPE(target);
    if (type != NULL_TREE && TYPE_PTR_P(type)) {
        type = TREE_TYPE(type);
    }
    if (type == NULL_TREE || !is_linear_type(type)) {
        return;
    }
    tree stripped = target;
    for (int depth = 0; stripped != NULL_TREE && depth < kTypeDepth; ++depth) {
        stripped = tree_strip_any_location_wrapper(stripped);
        if (TREE_CODE(stripped) != ADDR_EXPR && TREE_CODE(stripped) != NOP_EXPR) {
            break;
        }
        stripped = TREE_OPERAND(stripped, 0);
    }
    std::string object = "an object behind a reference";
    if (stripped != NULL_TREE && TREE_CODE(stripped) == COMPONENT_REF) {
        object = simple_name(TREE_OPERAND(stripped, 1));
    } else if (stripped != NULL_TREE && TREE_CODE(stripped) == ARRAY_REF) {
        object = "an element";
    } else if (stripped != NULL_TREE && DECL_P(stripped)) {
        object = simple_name(stripped);
    }
    record_shape(Kind::linear_untracked_assignment, location,
                 walk.name + ": " + object + " (" + linear_type_text(type) + ")");
}

// The rules at a call: drop-door, and the call graph of brand-reentry.
void check_call(tree call, ShapeWalk& walk) {
    tree callee = cp_get_callee_fndecl_nofold(call);
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL) {
        return;
    }
    location_t location = walk.location_of(call);
    int count = argument_count(call);
    call_graph.callees[DECL_UID(walk.function)].push_back(DECL_UID(callee));
    for (int index = 0; index < count; ++index) {
        tree argument = argument_at(call, index);
        tree type = argument != NULL_TREE ? TREE_TYPE(argument) : NULL_TREE;
        if (type == NULL_TREE || (index == 0 && DECL_IOBJ_MEMBER_FUNCTION_P(callee))) {
            continue;
        }
        if (tree brand = unbound_brand(type, walk.function)) {
            call_graph.branded_calls.push_back(
                BrandedCall{DECL_UID(walk.function), DECL_UID(callee), location,
                            walk.name + ": " + function_text(callee) + ", " + brand_text(brand)});
            break;
        }
    }
    if (walk.is_exempt_from_drops || count < 1 || TREE_CODE(call) != CALL_EXPR) {
        return;
    }
    tree first = argument_at(call, 0);
    if (DECL_DESTRUCTOR_P(callee) && is_base_union_member(walk.function)) {
        if (DECL_CONTEXT(callee) != NULL_TREE && TYPE_P(DECL_CONTEXT(callee)) && is_linear_type(DECL_CONTEXT(callee))
            && !is_frame_object(first, ObjectForm::address)) {
            record_shape(Kind::linear_destroy, location, walk.name + ": the end of an alternative");
        }
        return;
    }
    if (DECL_ASSIGNMENT_OPERATOR_P(callee) && DECL_CONTEXT(callee) != NULL_TREE && TYPE_P(DECL_CONTEXT(callee))
        && is_linear_type(DECL_CONTEXT(callee))) {
        check_assignment_target(first, ObjectForm::address, location, walk);
    } else if (DECL_DESTRUCTOR_P(callee) && DECL_CONTEXT(callee) != NULL_TREE && TYPE_P(DECL_CONTEXT(callee))
               && is_linear_type(DECL_CONTEXT(callee)) && !is_frame_object(first, ObjectForm::address)) {
        // A delete-expression calls the destructor too, or clobbers the
        // object of a trivial destructor (walk_shapes).
        record_shape(Kind::linear_destroy, location,
                     walk.name + ": " + function_text(callee) + " (" + linear_type_text(DECL_CONTEXT(callee)) + ")");
    }
}

// A local of the body: a class of the library with a linear argument, a
// container that holds a fresh brand, and a function pointer whose type holds
// one.  A local whose initializer is a call that gives the same type takes the
// type of that callee, so the signature of the callee holds the finding.
void check_local(tree decl, const ShapeWalk& walk) {
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE || type == error_mark_node || DECL_ARTIFICIAL(decl) || uses_template_parms(type)) {
        return;
    }
    check_class(type);
    bool is_from_call = walk.from_call.count(decl) != 0 || is_call_of_type(DECL_INITIAL(decl), type);
    std::string entity = walk.name + ": " + simple_name(decl);
    tree holder = library_holder_of(type);
    if (holder != NULL_TREE && !is_from_call) {
        record_shape(Kind::linear_library_holder, DECL_SOURCE_LOCATION(decl),
                     entity + ", " + qualified_name(TYPE_MAIN_DECL(holder)));
    }
    check_arena_object(decl, entity, DECL_SOURCE_LOCATION(decl), false);
    tree brand = unbound_brand(type, walk.function);
    if (brand == NULL_TREE) {
        return;
    }
    tree bare = TYPE_MAIN_VARIANT(type);
    if (TYPE_PTR_P(bare) && FUNC_OR_METHOD_TYPE_P(TREE_TYPE(bare))) {
        record_shape(Kind::brand_escaped_function_pointer, DECL_SOURCE_LOCATION(decl),
                     entity + ", " + brand_text(brand));
    } else if (CLASS_TYPE_P(bare) && is_library_class(bare) && !CP_AGGREGATE_TYPE_P(bare) && !is_from_call) {
        record_shape(Kind::brand_escaped_container, DECL_SOURCE_LOCATION(decl), entity + ", " + brand_text(brand));
    }
}

// A variable outside each function that a body names, such as a static data
// member or an instance of a variable template: each one has static or thread
// storage duration.  A scope brand in its type is the kind scope-static (the
// scope brands).  The walk of a body reads each local of static storage
// duration in its BIND_EXPR.
void check_named_static(tree decl, const ShapeWalk& walk) {
    if (!VAR_P(decl) || decl_function_context(decl) != NULL_TREE) {
        return;
    }
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE || type == error_mark_node || uses_template_parms(type)) {
        return;
    }
    std::vector<tree> held;
    collect_brands(type, held);
    location_t location = walk.last_location != UNKNOWN_LOCATION ? walk.last_location : DECL_SOURCE_LOCATION(decl);
    if (tree scope = first_scope_brand(held)) {
        record_shape(Kind::brand_scope_static, location,
                     walk.name + ": " + qualified_name(decl) + ", " + brand_text(scope));
    }
    check_arena_object(decl, walk.name + ": " + qualified_name(decl), location, true);
}

void note_linear_variable(tree variable, ShapeWalk& walk) {
    if (VAR_P(variable) && !TREE_STATIC(variable) && is_linear_or_reference(TREE_TYPE(variable))) {
        walk.has_linear = true;
    }
}

tree walk_shapes(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    auto& walk = *static_cast<ShapeWalk*>(data);
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node) || DECL_P(node)) {
        *walk_subtrees = 0;
        note_linear_variable(node, walk);
        check_named_static(node, walk);
        return NULL_TREE;
    }
    if (EXPR_P(node) && EXPR_HAS_LOCATION(node)) {
        walk.last_location = EXPR_LOCATION(node);
    }
    switch (TREE_CODE(node)) {
        case BIND_EXPR:
            for (tree variable = BIND_EXPR_VARS(node); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
                note_linear_variable(variable, walk);
                if (VAR_P(variable) && TREE_STATIC(variable) && !DECL_EXTERNAL(variable)) {
                    check_static_object(variable, walk.name);
                }
            }
            break;
        case DECL_EXPR: {
            tree decl = DECL_EXPR_DECL(node);
            if (decl != NULL_TREE && VAR_P(decl) && !TREE_STATIC(decl) && !DECL_EXTERNAL(decl)) {
                walk.locals.push_back(decl);
                // The walk stops at a declaration, so it reads the
                // initializer here, such as the closure of a lambda.
                if (DECL_INITIAL(decl) != NULL_TREE && DECL_INITIAL(decl) != error_mark_node) {
                    cp_walk_tree(&DECL_INITIAL(decl), walk_shapes, data, &walk.visited);
                }
            }
            break;
        }
        case INIT_EXPR: {
            tree target = tree_strip_any_location_wrapper(TREE_OPERAND(node, 0));
            if (VAR_P(target) && is_call_of_type(TREE_OPERAND(node, 1), TREE_TYPE(target))) {
                walk.from_call.insert(target);
            }
            // A lambda initializes each capture of its closure with one
            // initialization of a field.
            if (TREE_CODE(target) == COMPONENT_REF) {
                tree closure = TREE_TYPE(TREE_OPERAND(target, 0));
                if (is_linear_reference_capture(closure, TREE_OPERAND(target, 1))) {
                    record_capture(closure, TREE_OPERAND(target, 1), TREE_OPERAND(node, 1), walk);
                }
            }
            break;
        }
        case CLEANUP_STMT:
            // The cleanup of a local is its end of scope, which must-consume
            // reads.
            cp_walk_tree(&CLEANUP_BODY(node), walk_shapes, data, &walk.visited);
            *walk_subtrees = 0;
            return NULL_TREE;
        case TARGET_EXPR:
            cp_walk_tree(&TARGET_EXPR_INITIAL(node), walk_shapes, data, &walk.visited);
            *walk_subtrees = 0;
            if (is_linear_type(TREE_TYPE(node))) {
                walk.has_linear = true;
            }
            return NULL_TREE;
        case CONSTRUCTOR:
            if (LAMBDA_TYPE_P(TREE_TYPE(node))) {
                check_captures(node, walk);
            }
            break;
        case AGGR_INIT_EXPR: {
            // A call that builds its value in a local, with no copy.
            tree slot = AGGR_INIT_EXPR_SLOT(node);
            if (slot != NULL_TREE && VAR_P(slot) && !AGGR_INIT_VIA_CTOR_P(node)
                && is_call_of_type(node, TREE_TYPE(slot))) {
                walk.from_call.insert(slot);
            }
            check_call(node, walk);
            break;
        }
        case CALL_EXPR:
            check_call(node, walk);
            break;
        case MODIFY_EXPR:
            // An explicit call of a trivial destructor is a clobber of its
            // object.
            if (TREE_CLOBBER_P(TREE_OPERAND(node, 1))) {
                tree target = TREE_OPERAND(node, 0);
                if (!walk.is_exempt_from_drops && TREE_TYPE(target) != NULL_TREE && is_linear_type(TREE_TYPE(target))
                    && !is_frame_object(target, ObjectForm::object)) {
                    std::string const ended = is_base_union_member(walk.function)
                                                ? std::string{"the end of an alternative"}
                                                : "the end of a lifetime (" + linear_type_text(TREE_TYPE(target)) + ")";
                    record_shape(Kind::linear_destroy, walk.location_of(node), walk.name + ": " + ended);
                }
            } else {
                check_assignment_target(TREE_OPERAND(node, 0), ObjectForm::object, walk.location_of(node), walk);
            }
            break;
        default:
            break;
    }
    if ((EXPR_P(node) || TREE_CODE(node) == CONSTRUCTOR) && TREE_TYPE(node) != NULL_TREE
        && (is_linear_type(TREE_TYPE(node)) || is_linear_move_type(TREE_TYPE(node)))) {
        walk.has_linear = true;
    }
    if (is_move_assignment_call(node) || calls_with_moving_closure(node)) {
        walk.has_linear = true;
    }
    return NULL_TREE;
}

// The rule brand-reentry at the end of the unit: a call with a branded
// argument whose callee can reach the caller again.  Complexity: one search
// of the call graph for each branded call, O(V + E) each.
void report_reentries() {
    for (const BrandedCall& call : call_graph.branded_calls) {
        std::unordered_set<int> seen;
        std::vector<int> pending{call.callee};
        bool reaches = false;
        while (!pending.empty() && !reaches) {
            int current = pending.back();
            pending.pop_back();
            if (current == call.caller) {
                reaches = true;
            } else if (seen.insert(current).second) {
                auto found = call_graph.callees.find(current);
                if (found != call_graph.callees.end()) {
                    pending.insert(pending.end(), found->second.begin(), found->second.end());
                }
            }
        }
        if (reaches) {
            record_shape(Kind::brand_reentry, call.location, call.entity);
        }
    }
}

// ── The bodies ──────────────────────────────────────────────────────────

// True when a body holds a linear object or a linear value, from a quick walk,
// in a unit that runs must-consume and not the shapes.  Most bodies hold none,
// and the analysis then does not run.
tree find_linear(tree* node_pointer, int* walk_subtrees, void*) {
    tree node = *node_pointer;
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node) || DECL_P(node)) {
        *walk_subtrees = 0;
        if (VAR_P(node) && !TREE_STATIC(node) && is_linear_or_reference(TREE_TYPE(node))) {
            return node;
        }
        return NULL_TREE;
    }
    if (TREE_CODE(node) == BIND_EXPR) {
        for (tree variable = BIND_EXPR_VARS(node); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
            if (VAR_P(variable) && !TREE_STATIC(variable) && is_linear_or_reference(TREE_TYPE(variable))) {
                return variable;
            }
        }
    }
    if ((EXPR_P(node) || TREE_CODE(node) == CONSTRUCTOR) && TREE_TYPE(node) != NULL_TREE
        && (is_linear_type(TREE_TYPE(node)) || is_linear_move_type(TREE_TYPE(node)))) {
        return node;
    }
    if (is_move_assignment_call(node) || calls_with_moving_closure(node)) {
        return node;
    }
    return NULL_TREE;
}

// True when FUNCTION has a parameter that the rule tracks: a linear class by
// value, an rvalue reference to one, or a reference that is not const to one,
// which the caller lends (the lent parameters).  A move of a lent parameter
// names the reference itself, so the body can hold no expression of a linear
// class.
bool has_linear_parameter(tree function) {
    for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        tree type = TREE_TYPE(parameter);
        if (type != NULL_TREE && TYPE_REF_P(type) && !TYPE_REF_IS_RVALUE(type) && TYPE_READONLY(TREE_TYPE(type))) {
            continue;
        }
        if (is_linear_or_reference(type)) {
            return true;
        }
    }
    return false;
}

// True when FUNCTION carries the annotation drop_door, detach_door or
// once_door.  The once door ends the closure that it calls (the doors).
bool carries_door_annotation(tree function) {
    tree attributes = DECL_ATTRIBUTES(function);
    return carries_annotation(attributes, linear_names.drop_door)
        || carries_annotation(attributes, linear_names.detach_door)
        || carries_annotation(attributes, linear_names.once_door);
}

// The linear-door row that names FUNCTION, or null.
const LinearDoorRow* door_row_of(tree function) {
    if (state.table.linear_doors.empty()) {
        return nullptr;
    }
    std::string name = qualified_name(function);
    for (const LinearDoorRow& row : state.table.linear_doors) {
        if (row.function == name) {
            return &row;
        }
    }
    return nullptr;
}

// True when FUNCTION is a door (the doors).  A linear-door row that names
// FUNCTION gives its record at the place of FUNCTION.
bool is_door(tree function, const Place& place) {
    if (carries_door_annotation(function)) {
        return true;
    }
    const LinearDoorRow* row = door_row_of(function);
    if (row != nullptr) {
        add_finding(Kind::linear_door, nullptr, place, row->function);
    }
    return row != nullptr;
}

bool has_scope_door(tree type) {
    if (carries_annotation(TYPE_ATTRIBUTES(type), linear_names.scope_end_door)) {
        return true;
    }
    tree destructor = CLASSTYPE_DESTRUCTOR(type);
    return destructor != NULL_TREE && TREE_CODE(destructor) == FUNCTION_DECL
        && (carries_door_annotation(destructor) || door_row_of(destructor) != nullptr);
}

// The DECL_UID of the field of *this that the expression NODE names through
// casts and member accesses, the innermost field that is no base, or
// kWholeObject when NODE names *this itself or no member of *this (the
// accessors).  OBJECT is the object parameter of the function.
int member_field_of(tree node, tree object) {
    tree leaf = NULL_TREE;
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        switch (TREE_CODE(node)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case VIEW_CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case ADDR_EXPR:
            case INDIRECT_REF:
            case SAVE_EXPR:
            case CLEANUP_POINT_EXPR:
                node = TREE_OPERAND(node, 0);
                continue;
            case COMPONENT_REF:
                if (TREE_CODE(TREE_OPERAND(node, 1)) == FIELD_DECL && !DECL_FIELD_IS_BASE(TREE_OPERAND(node, 1))) {
                    leaf = TREE_OPERAND(node, 1);
                }
                node = TREE_OPERAND(node, 0);
                continue;
            case PARM_DECL:
                return node == object && leaf != NULL_TREE ? static_cast<int>(DECL_UID(leaf)) : kWholeObject;
            default:
                return kWholeObject;
        }
    }
    return kWholeObject;
}

// The state of the walk of the return statements of one accessor.
struct ReturnWalk {
    tree object = NULL_TREE;
    int field = kWholeObject;
    bool saw_return = false;
};

tree walk_returns(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    if (node == NULL_TREE || TYPE_P(node) || TREE_CODE(node) == LAMBDA_EXPR) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    if (TREE_CODE(node) != RETURN_EXPR) {
        return NULL_TREE;
    }
    auto& walk = *static_cast<ReturnWalk*>(data);
    tree value = TREE_OPERAND(node, 0);
    if (value != NULL_TREE && (TREE_CODE(value) == INIT_EXPR || TREE_CODE(value) == MODIFY_EXPR)) {
        value = TREE_OPERAND(value, 1);
    }
    int field = member_field_of(value, walk.object);
    walk.field = !walk.saw_return || walk.field == field ? field : kWholeObject;
    walk.saw_return = true;
    return NULL_TREE;
}

// Keeps the field of *this that the accessor FUNCTION gives (the accessors):
// the field of each of its return statements when they agree, else the
// object.  Complexity: linear in the body.
void note_accessor_field(tree function) {
    ReturnWalk walk;
    walk.object = DECL_ARGUMENTS(function);
    tree body = DECL_SAVED_TREE(function);
    cp_walk_tree_without_duplicates(&body, walk_returns, &walk);
    accessor_fields[static_cast<int>(DECL_UID(function))] = walk.saw_return ? walk.field : kWholeObject;
}

// The capture field of CLOSURE that NODE names in the body of its call
// operator, through the casts, a dereference, an address and a capture
// proxy, or null.
tree capture_field_of(tree node, tree closure) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        switch (TREE_CODE(node)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case VIEW_CONVERT_EXPR:
            case INDIRECT_REF:
            case ADDR_EXPR:
                node = TREE_OPERAND(node, 0);
                continue;
            case VAR_DECL:
                if (!DECL_HAS_VALUE_EXPR_P(node)) {
                    return NULL_TREE;
                }
                node = DECL_VALUE_EXPR(node);
                continue;
            case COMPONENT_REF: {
                tree field = TREE_OPERAND(node, 1);
                return TREE_CODE(field) == FIELD_DECL && TYPE_MAIN_VARIANT(DECL_CONTEXT(field)) == closure ? field
                                                                                                           : NULL_TREE;
            }
            default:
                return NULL_TREE;
        }
    }
    return NULL_TREE;
}

struct ClosureFillWalk {
    tree closure = NULL_TREE;
    std::vector<unsigned> fills;
};

void note_fill(ClosureFillWalk& walk, tree field) {
    if (field != NULL_TREE && is_sum_capture(field)
        && std::find(walk.fills.begin(), walk.fills.end(), DECL_UID(field)) == walk.fills.end()) {
        walk.fills.push_back(DECL_UID(field));
    }
}

// The uses of the body of a call operator that can fill a captured sum: an
// argument of a parameter that writes it, and the target of an assignment.
tree walk_closure_fills(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    if (node == NULL_TREE || TYPE_P(node) || TREE_CODE(node) == LAMBDA_EXPR) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    auto& walk = *static_cast<ClosureFillWalk*>(data);
    if (TREE_CODE(node) == MODIFY_EXPR || TREE_CODE(node) == INIT_EXPR) {
        note_fill(walk, capture_field_of(TREE_OPERAND(node, 0), walk.closure));
        return NULL_TREE;
    }
    if (TREE_CODE(node) != CALL_EXPR) {
        return NULL_TREE;
    }
    tree callee = cp_get_callee_fndecl_nofold(node);
    bool is_function = callee != NULL_TREE && TREE_CODE(callee) == FUNCTION_DECL;
    tree type = function_type_of(node, is_function ? callee : NULL_TREE);
    bool is_method = type != NULL_TREE && TREE_CODE(type) == METHOD_TYPE;
    // A consumer and a member that empties the sum fill nothing.
    bool is_emptying_member = is_method
                           && (FUNCTION_RVALUE_QUALIFIED(type)
                               || (is_function && carries_linear_annotation(callee, linear_names.empties)));
    tree parameter = type != NULL_TREE ? TYPE_ARG_TYPES(type) : NULL_TREE;
    for (int index = 0; index < argument_count(node); ++index) {
        tree parameter_type = parameter != NULL_TREE ? TREE_VALUE(parameter) : NULL_TREE;
        if (parameter != NULL_TREE) {
            parameter = TREE_CHAIN(parameter);
        }
        if (is_writing_parameter(parameter_type) && !(index == 0 && is_emptying_member)) {
            note_fill(walk, capture_field_of(argument_at(node, index), walk.closure));
        }
    }
    return NULL_TREE;
}

// Keeps the capture fields of the closure of the call operator FUNCTION whose
// sum its body can fill (the summaries).  Complexity: linear in the body.
void note_closure_fills(tree function) {
    tree closure = DECL_CONTEXT(function);
    if (closure == NULL_TREE || !LAMBDA_TYPE_P(closure)) {
        return;
    }
    ClosureFillWalk walk;
    walk.closure = TYPE_MAIN_VARIANT(closure);
    tree body = DECL_SAVED_TREE(function);
    cp_walk_tree_without_duplicates(&body, walk_closure_fills, &walk);
    std::vector<unsigned>& kept = closure_fills[TYPE_UID(walk.closure)];
    for (unsigned field : walk.fills) {
        if (std::find(kept.begin(), kept.end(), field) == kept.end()) {
            kept.push_back(field);
        }
    }
}

struct KeptReferenceWalk {
    tree parameter = NULL_TREE;
    int uses = 0;
    int read_uses = 0;
    // The return statements that give the parameter back as the result.
    int return_uses = 0;
    // The callees with no fact at this point that take the parameter by a
    // reference that writes.  The fact of the body waits for theirs.
    std::vector<unsigned> waits_for;
};

// The uses of PARAMETER in a body, and the uses that keep nothing: the object
// argument of a member call, and an argument of a call whose callee keeps no
// reference, or whose parameter does not write.  A callee with no fact at
// this point keeps nothing when its own fact says so at the end of the unit.
tree walk_kept_reference(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    auto& walk = *static_cast<KeptReferenceWalk*>(data);
    if (node == NULL_TREE || TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    if (node == walk.parameter) {
        ++walk.uses;
        return NULL_TREE;
    }
    if (TREE_CODE(node) == RETURN_EXPR) {
        // The result gives the parameter back when it names the parameter, or
        // the result of a callee that can give the parameter back: a callee
        // with that fact, or with no fact at this point.
        tree value = TREE_OPERAND(node, 0);
        for (int depth = 0; value != NULL_TREE && depth < kTypeDepth; ++depth) {
            value = tree_strip_any_location_wrapper(value);
            if (TREE_CODE(value) == INIT_EXPR || TREE_CODE(value) == MODIFY_EXPR) {
                value = TREE_OPERAND(value, 1);
            } else if (TREE_CODE(value) == NOP_EXPR || TREE_CODE(value) == CONVERT_EXPR
                       || TREE_CODE(value) == NON_LVALUE_EXPR || TREE_CODE(value) == ADDR_EXPR
                       || TREE_CODE(value) == INDIRECT_REF) {
                value = TREE_OPERAND(value, 0);
            } else if (TREE_CODE(value) == CALL_EXPR) {
                tree inner = cp_get_callee_fndecl_nofold(value);
                bool can_give_back =
                    inner != NULL_TREE && TREE_CODE(inner) == FUNCTION_DECL
                    && (returns_sum_reference.count(DECL_UID(inner)) != 0 || kept_fact_of(inner) == KeptFact::unknown);
                bool is_passed = false;
                for (int index = 0; can_give_back && index < call_expr_nargs(value); ++index) {
                    tree argument = CALL_EXPR_ARG(value, index);
                    while (argument != NULL_TREE
                           && (TREE_CODE(argument) == NOP_EXPR || TREE_CODE(argument) == ADDR_EXPR
                               || TREE_CODE(argument) == INDIRECT_REF || TREE_CODE(argument) == CONVERT_EXPR)) {
                        argument = TREE_OPERAND(argument, 0);
                    }
                    is_passed = is_passed || argument == walk.parameter;
                }
                value = is_passed ? walk.parameter : NULL_TREE;
                break;
            } else {
                break;
            }
        }
        if (value == walk.parameter) {
            ++walk.return_uses;
        }
        return NULL_TREE;
    }
    if (TREE_CODE(node) != CALL_EXPR) {
        return NULL_TREE;
    }
    tree callee = cp_get_callee_fndecl_nofold(node);
    bool is_function = callee != NULL_TREE && TREE_CODE(callee) == FUNCTION_DECL;
    tree type = function_type_of(node, is_function ? callee : NULL_TREE);
    bool is_method = type != NULL_TREE && TREE_CODE(type) == METHOD_TYPE;
    KeptFact fact = kept_fact_of(is_function ? callee : NULL_TREE);
    tree parameter = type != NULL_TREE ? TYPE_ARG_TYPES(type) : NULL_TREE;
    for (int index = 0; index < argument_count(node); ++index) {
        tree parameter_type = parameter != NULL_TREE ? TREE_VALUE(parameter) : NULL_TREE;
        if (parameter != NULL_TREE) {
            parameter = TREE_CHAIN(parameter);
        }
        bool is_plain_read = (is_method && index == 0) || !is_writing_parameter(parameter_type);
        bool is_reading = is_plain_read || fact != KeptFact::can_keep;
        tree argument = argument_at(node, index);
        for (int depth = 0; argument != NULL_TREE && depth < kTypeDepth; ++depth) {
            argument = tree_strip_any_location_wrapper(argument);
            if (TREE_CODE(argument) == NOP_EXPR || TREE_CODE(argument) == CONVERT_EXPR
                || TREE_CODE(argument) == INDIRECT_REF || TREE_CODE(argument) == ADDR_EXPR) {
                argument = TREE_OPERAND(argument, 0);
                continue;
            }
            break;
        }
        if (argument == walk.parameter && is_reading) {
            ++walk.read_uses;
            unsigned uid = is_function ? DECL_UID(callee) : 0;
            if (!is_plain_read && fact == KeptFact::unknown
                && std::find(walk.waits_for.begin(), walk.waits_for.end(), uid) == walk.waits_for.end()) {
                walk.waits_for.push_back(uid);
            }
        }
    }
    return NULL_TREE;
}

// Keeps the function FUNCTION when its body keeps no reference parameter that
// names a linear sum: each use of each such parameter is a read (the
// summaries).  A body that passes such a parameter to a callee with no fact at
// this point gets a fact that waits for the fact of that callee.  Complexity:
// linear in the nodes of the body, for each such parameter.
void note_kept_references(tree function) {
    unsigned uid = DECL_UID(function);
    summarized_functions.insert(uid);
    tree body = DECL_SAVED_TREE(function);
    std::vector<unsigned> waits_for;
    for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        tree type = TREE_TYPE(parameter);
        // A body that only names a reference needs no complete class, so the
        // class of the parameter can be an instance that the unit completes
        // later, such as an Option.  Such a class can be a linear sum.
        tree target = is_writing_parameter(type) ? TREE_TYPE(type) : NULL_TREE;
        bool can_be_sum =
            target != NULL_TREE && (is_linear_sum(target) || (CLASS_TYPE_P(target) && !COMPLETE_TYPE_P(target)));
        if (!can_be_sum) {
            continue;
        }
        KeptReferenceWalk walk;
        walk.parameter = parameter;
        cp_walk_tree(&body, walk_kept_reference, &walk, nullptr);
        // A body that gives the reference back as its result keeps it only in
        // the result, and the caller reads the result (the aliases).
        if (walk.uses > walk.read_uses + walk.return_uses) {
            return;
        }
        if (walk.return_uses > 0) {
            returns_sum_reference.insert(uid);
        }
        for (unsigned callee : walk.waits_for) {
            if (std::find(waits_for.begin(), waits_for.end(), callee) == waits_for.end()) {
                waits_for.push_back(callee);
            }
        }
    }
    if (waits_for.empty()) {
        keeps_no_sum_reference.insert(uid);
    } else {
        waiting_kept_facts[uid] = std::move(waits_for);
    }
}

void on_pre_genericize_linear(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    // A clone of a constructor or a destructor gets a second event on a copy
    // of the body that cp_genericize lowered (cp/optimize.cc,
    // maybe_clone_body).  The abstract function had its own event.
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_SAVED_TREE(function) == NULL_TREE
        || DECL_CLONED_FUNCTION_P(function) || seen_error()) {
        return;
    }
    if (DECL_ARTIFICIAL(function) && !LAMBDA_FUNCTION_P(function)) {
        return;
    }
    if (DECL_LANG_SPECIFIC(function) != nullptr && DECL_DEFAULTED_FN(function)) {
        return;
    }
    // The fact of an accessor serves each caller in this unit, also when the
    // rule does not read the body of the accessor here (the accessors).
    if (linear_mode.checks_must_consume && is_accessor(function)) {
        note_accessor_field(function);
    }
    // The summaries of a closure and of a callee serve each caller in this
    // unit (the summaries).
    if (linear_mode.checks_must_consume) {
        if (LAMBDA_FUNCTION_P(function)) {
            note_closure_fills(function);
            note_closure_summary(function);
        } else {
            note_named_body_summary(function);
        }
        note_kept_references(function);
    }
    // The rule reads the bodies that is_body_read_here of plugin.h gives (the
    // bodies), and the place of the function is the place of its findings.
    if (!is_body_read_here(function, UNKNOWN_LOCATION)) {
        return;
    }
    Place place = place_of(DECL_SOURCE_LOCATION(function), Scope::quarantine);
    tree body = DECL_SAVED_TREE(function);
    bool has_linear = has_linear_parameter(function);
    if (linear_mode.checks_shapes) {
        check_signature(function);
        ShapeWalk walk;
        walk.function = function;
        walk.name = function_text(function);
        bool is_door_function = carries_door_annotation(function) || door_row_of(function) != nullptr;
        walk.is_exempt_from_drops = is_door_function || is_transfer_function(function) || DECL_DESTRUCTOR_P(function);
        cp_walk_tree(&body, walk_shapes, &walk, &walk.visited);
        for (tree local : walk.locals) {
            check_local(local, walk);
        }
        has_linear = has_linear || walk.has_linear;
    } else if (!has_linear) {
        has_linear = cp_walk_tree_without_duplicates(&body, find_linear, nullptr) != NULL_TREE;
    }
    if (!linear_mode.checks_must_consume || !has_linear) {
        return;
    }
    analyze_body(function, is_door(function, place));
}

// PLUGIN_FINISH_TYPE: the class rules for each class that the unit defines
// and that is no pattern (the shapes).
void on_finish_type_linear(void* gcc_data, void*) {
    tree type = static_cast<tree>(gcc_data);
    if (type == NULL_TREE || type == error_mark_node || !TYPE_P(type) || seen_error()) {
        return;
    }
    check_class(type);
}

// PLUGIN_FINISH_DECL: a variable with static storage duration at namespace
// scope (the shapes).  The walk of a body reads each local, and the walk of
// the declarations reads each alias (check_linear_alias).
void on_finish_decl_linear(void* gcc_data, void*) {
    tree decl = static_cast<tree>(gcc_data);
    if (decl == NULL_TREE || decl == error_mark_node || !VAR_P(decl) || seen_error()
        || TREE_CODE(CP_DECL_CONTEXT(decl)) != NAMESPACE_DECL || is_erroneous_decl(decl)) {
        return;
    }
    bool is_static_object = (TREE_STATIC(decl) || DECL_THREAD_LOCAL_P(decl)) && !DECL_EXTERNAL(decl);
    if (is_static_object && is_reported_for(decl)) {
        check_static_object(decl, qualified_name(CP_DECL_CONTEXT(decl)));
    }
}

// The linear local or parameter that NODE moves through casts and std::move,
// or null (the dropped elements).
tree moved_linear_decl(tree node) {
    bool saw_rvalue = false;
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        switch (TREE_CODE(node)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case VIEW_CONVERT_EXPR:
            case IMPLICIT_CONV_EXPR:
            case STATIC_CAST_EXPR:
                if (TREE_TYPE(node) != NULL_TREE && TYPE_REF_P(TREE_TYPE(node))
                    && TYPE_REF_IS_RVALUE(TREE_TYPE(node))) {
                    saw_rvalue = true;
                }
                node = TREE_OPERAND(node, 0);
                continue;
            case ADDR_EXPR:
            case INDIRECT_REF:
            case CLEANUP_POINT_EXPR:
            case SAVE_EXPR:
            case PAREN_EXPR:
                node = TREE_OPERAND(node, 0);
                continue;
            case TARGET_EXPR:
                node = TARGET_EXPR_INITIAL(node);
                continue;
            case CALL_EXPR: {
                tree callee = cp_get_callee_fndecl_nofold(node);
                if (!is_std_move_cast(callee) || call_expr_nargs(node) < 1) {
                    return NULL_TREE;
                }
                saw_rvalue = saw_rvalue || DECL_NAME(callee) != linear_names.forward;
                node = CALL_EXPR_ARG(node, 0);
                continue;
            }
            case VAR_DECL:
            case PARM_DECL:
                return saw_rvalue && !TREE_STATIC(node) && is_linear_or_reference(TREE_TYPE(node)) ? node : NULL_TREE;
            default:
                return NULL_TREE;
        }
    }
    return NULL_TREE;
}

tree find_located(tree* node_pointer, int* walk_subtrees, void*) {
    tree node = *node_pointer;
    if (node == NULL_TREE || TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    return EXPR_P(node) && EXPR_HAS_LOCATION(node) ? node : NULL_TREE;
}

// PLUGIN_FOLDED_INITIALIZER (the folds).  The fold of an element that moves a
// linear local or parameter is kept for the dropped elements, at the location
// of the first node of the original with one.
void on_folded_initializer(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_folded_initializer_data*>(gcc_data);
    if (data == nullptr || data->original == NULL_TREE || data->folded == NULL_TREE || data->original == data->folded
        || data->original == error_mark_node) {
        return;
    }
    folded_initializers[data->folded].push_back(FoldEntry{data->object, data->original});
    if (data->kind != PLUGIN_FOLDED_ELEMENT && data->kind != PLUGIN_FOLDED_ARRAY_ELEMENT) {
        return;
    }
    tree moved = moved_linear_decl(data->original);
    if (moved == NULL_TREE) {
        return;
    }
    tree original = data->original;
    tree located = cp_walk_tree_without_duplicates(&original, find_located, nullptr);
    location_t location = located != NULL_TREE ? EXPR_LOCATION(located) : data->location;
    element_folds.push_back(ElementFold{data->original, data->folded, moved, location});
}

// PLUGIN_EMPTY_CLASS_ASSIGNMENT (the folds).
void on_empty_class_assignment(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_empty_class_assignment_data*>(gcc_data);
    if (data == nullptr || data->source == NULL_TREE || data->target == NULL_TREE || data->source == error_mark_node) {
        return;
    }
    empty_assignment_of_source[data->source] = empty_assignments.size();
    empty_assignments.push_back(EmptyAssignment{data->target, data->source, data->location});
}

void on_linear_collection(void*, void*) {
    linear_caches.facts.clear();
    linear_caches.can_move.clear();
    linear_caches.holds_sum.clear();
    shape_caches.refuses_lifetime.clear();
    shape_caches.library_holder.clear();
    shape_caches.brands.clear();
    shape_caches.epoch_brands.clear();
    folded_initializers.clear();
    empty_assignments.clear();
    empty_assignment_of_source.clear();
    element_folds.clear();
}

}  // namespace

// An alias at namespace scope, from the walk of the declarations
// (declaration.cpp), because the front end gives no event PLUGIN_FINISH_DECL
// for an alias (the shapes).
void check_linear_alias(tree decl) {
    if (!linear_mode.checks_shapes || decl == NULL_TREE || TREE_CODE(decl) != TYPE_DECL
        || TREE_CODE(CP_DECL_CONTEXT(decl)) != NAMESPACE_DECL || !is_reported_for(decl)) {
        return;
    }
    check_alias(decl, CP_DECL_CONTEXT(decl), qualified_name(CP_DECL_CONTEXT(decl)));
}

// An object of static or thread storage duration, from the static walks of
// declaration.cpp: the kind option-static when its type is or holds a linear
// sum by value (the sum holders).
void check_static_linear_sum(tree decl, const std::string& entity) {
    if (!linear_mode.checks_must_consume || decl == NULL_TREE || !holds_linear_sum(TREE_TYPE(decl))) {
        return;
    }
    Place place = place_of(DECL_SOURCE_LOCATION(decl), Scope::quarantine);
    if (place.file == nullptr) {
        return;
    }
    add_finding(Kind::linear_option_static, nullptr, place, entity + " (" + linear_type_text(TREE_TYPE(decl)) + ")");
}

// The error of one finding of the kinds of the scope brands and the arena
// brands that no region opts out.
void report_scope_escape(const Finding& finding) {
    if (finding.kind == Kind::brand_scope_static) {
        error_at(finding.spelling,
                 "an object of static or thread storage duration holds a value of a scope brand, so the value "
                 "outlives the call of its mint: %s",
                 finding.entity.c_str());
        inform(finding.spelling,
               "one call site of a scope mint gives each of its calls one brand, so the object carries a value of "
               "one call into the next call, such as an index of a long view into a short view");
        inform(finding.spelling, "keep the value in an object of automatic storage duration in the body of the scope");
    } else if (finding.kind == Kind::brand_scope_return) {
        error_at(finding.spelling, "a scope mint gives a result that holds its own scope brand: %s",
                 finding.entity.c_str());
        inform(finding.spelling,
               "the result leaves the call of the mint, so a value of the brand outlives its scope, also when a "
               "capture of a closure or a field of a local class holds it");
        inform(finding.spelling, "give a result that holds no value of the scope, such as the number of an index");
    } else if (finding.kind == Kind::brand_arena_member) {
        error_at(finding.spelling, "a member arena tag does not name exactly one member of the class that nests it: %s",
                 finding.entity.c_str());
        inform(finding.spelling,
               "two arenas of one member tag share the brand, so a Ref of one fits a field that names the other");
        inform(finding.spelling,
               "nest the tag in the class beside its one member, which holds the arena itself, and give each other "
               "arena its own tag or the scope arena of %<with_bump_arena%>");
    } else if (finding.kind == Kind::brand_epoch_member) {
        error_at(finding.spelling, "a member holds an epoch brand that its class does not state: %s",
                 finding.entity.c_str());
        inform(finding.spelling,
               "the end of the epoch ends each byte that the epoch gave out, so only a carrier of the epoch, a class "
               "that states the brand as its %<brand_type%>, holds a value of it, and the rules then read the carrier");
        inform(finding.spelling,
               "state the epoch brand as the %<brand_type%> of the class, or keep the value in a local "
               "of the body of the epoch");
    } else if (finding.kind == Kind::brand_epoch_static) {
        error_at(finding.spelling, "an object of static or thread storage duration holds an epoch brand: %s",
                 finding.entity.c_str());
        inform(finding.spelling, "the object outlives the epoch, so a value of the epoch in it names ended bytes");
        inform(finding.spelling, "keep the value in a local of the body of the epoch");
    } else {
        return;
    }
    inform(finding.spelling,
           "a test of the escape itself puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> region of "
           "%<foundation/Quarantine.h%>");
}

// The error of one finding of the kinds option-unconsumed, option-discarded,
// option-known-full and option-known-empty (the sums), option-static (the sum
// holders) and borrow-move-out (the lent parameters), of the rule
// must-consume that no region opts out.  The message does not start with
// "quarantine: ", so a regex of a negative fixture reads it.
void report_option_end(const Finding& finding) {
    if (finding.kind == Kind::linear_option_unconsumed) {
        error_at(finding.spelling, "a linear sum can end with a payload that no consumer took: %s",
                 finding.entity.c_str());
        inform(finding.spelling,
               "an Option, a Result or a OneOf of a linear payload gives the payload out only through a consumer, "
               "so its end with a payload drops a value that each path must use one time");
        inform(finding.spelling,
               "consume the sum on each path, or test an Option empty before its end, or end an empty Option with "
               "%<expect_none(reason)%>");
    } else if (finding.kind == Kind::linear_option_discarded) {
        error_at(finding.spelling, "the code discards a temporary linear sum, so its payload can drop: %s",
                 finding.entity.c_str());
        inform(finding.spelling, "give the temporary to a consumer, such as expect, the rvalue match, a combinator, or "
                                 "%<expect_none(reason)%> for an empty Option");
    } else if (finding.kind == Kind::linear_option_known_full) {
        error_at(finding.spelling, "a door that needs an empty linear sum runs on a sum that each path shows full: %s",
                 finding.entity.c_str());
        inform(finding.spelling,
               "its run-time check ends the process on each path, because the sum holds a payload that the door "
               "would drop: consume the payload first, or use replace, which gives the old payload back");
    } else if (finding.kind == Kind::linear_option_known_empty) {
        error_at(finding.spelling, "a door that needs a full linear sum runs on a sum that each path shows empty: %s",
                 finding.entity.c_str());
        inform(finding.spelling,
               "its run-time check ends the process on each path, because the sum holds no payload: fill it first, or "
               "end the empty sum with %<expect_none(reason)%>");
    } else if (finding.kind == Kind::linear_option_static) {
        error_at(finding.spelling,
                 "an object of static or thread storage duration holds a linear sum, and the end of the process or "
                 "of the thread ends it with no consume: %s",
                 finding.entity.c_str());
        inform(finding.spelling,
               "a full sum drops at that end a payload that each path must use one time, and no rule reads the state "
               "of the sum there");
        inform(finding.spelling,
               "hold the sum in an object of automatic storage duration, or in an owner that a consumer ends");
    } else if (finding.kind == Kind::linear_borrow_move_out) {
        error_at(finding.spelling,
                 "a function moves a linear value out of a parameter that its caller lends, and a path leaves the "
                 "parameter spent: %s",
                 finding.entity.c_str());
        inform(finding.spelling,
               "the caller keeps a spent value that looks the same as a live one, so its next move gives a second "
               "value of one value, such as a second exclusive token of one region");
        inform(finding.spelling,
               "take the value with %<foundation::linear::exchange%>, which leaves a replacement in the parameter, "
               "give the parameter a value again on each path, or take the parameter by value or by %<T&&%>.  For "
               "the object %<this%> of a member function, give the function the qualifier %<&&%>");
    } else {
        return;
    }
    inform(finding.spelling,
           "a test of the drop itself puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> region of "
           "%<foundation/Quarantine.h%>");
}

// The error of one finding of the kind self-move that no region opts out (the
// self-moves).
void report_self_move(const Finding& finding) {
    if (finding.kind != Kind::linear_self_move) {
        return;
    }
    error_at(finding.spelling,
             "a move assignment moves an object into itself, so the move keeps nothing that the target owes: %s",
             finding.entity.c_str());
    inform(finding.spelling,
           "the target and the source of the assignment name one object after the casts, the dereferences, "
           "%<std::move%>, %<std::forward%> and a local reference");
    inform(finding.spelling, "remove the assignment, or move the value into a different object");
}

// The generic closures: each waiting line of a once door whose closure type
// leaves a linear capture live in one instance of its call operator.  The
// named bodies: each waiting line of a named body whose call operator && no
// summary read in the unit, as a body with a plain call operator or with no
// body in the unit, fails closed.  The two walks of one body give the same
// line, so the end keeps it one time.  Complexity: quadratic in the waiting
// lines, which are few in a unit.
void judge_waiting_owned_closures() {
    std::vector<WalkFinding> owned;
    for (const WaitingOwnedClosure& waiting : waiting_owned_closures) {
        bool const leaves_capture = closure_types_leave_capture.count(waiting.closure_type) != 0;
        bool const has_no_summary = waiting.fails_closed && named_bodies_summarized.count(waiting.closure_type) == 0;
        if ((leaves_capture || has_no_summary)
            && std::find(owned.begin(), owned.end(), waiting.finding) == owned.end()) {
            owned.push_back(waiting.finding);
        }
    }
    add_walk_findings(owned);
    waiting_owned_closures.clear();
}

// The rule brand-reentry at the end of the unit (plugin.cpp, walk_unit): each
// body of the unit gave its edges of the call graph by then.
void finish_linear_rules() {
    if (linear_mode.checks_must_consume) {
        judge_deferred_findings();
        judge_waiting_owned_closures();
    }
    if (linear_mode.checks_shapes && !call_graph.was_reported && !seen_error()) {
        call_graph.was_reported = true;
        report_reentries();
    }
}

// Registers the rules of the family linear (plugin.cpp, the arguments
// must-consume=on and linear-shapes=on).
void register_linear(const char* plugin_name, bool checks_must_consume, bool checks_shapes) {
    linear_mode.checks_must_consume = checks_must_consume;
    linear_mode.checks_shapes = checks_shapes;
    linear_names.linear = get_identifier("linear");
    linear_names.lifetime = get_identifier("lifetime");
    linear_names.no_start_over_bytes = get_identifier("no_start_over_bytes");
    linear_names.brand_type = get_identifier("brand_type");
    linear_names.brand = get_identifier("brand");
    linear_names.scope_brand = get_identifier("scope_brand");
    linear_names.epoch_brand = get_identifier("epoch_brand");
    linear_names.arena_member_brand = get_identifier("arena_member_brand");
    linear_names.branded_arena = get_identifier("branded_arena");
    linear_names.transfer = get_identifier("transfer");
    linear_names.refers = get_identifier("refers");
    linear_names.refers_unique = get_identifier("refers_unique");
    linear_names.refers_deep = get_identifier("refers_deep");
    linear_names.by_arguments = get_identifier("by_arguments");
    linear_names.payload_cell = get_identifier("payload_cell");
    linear_names.drop_door = get_identifier("drop_door");
    linear_names.detach_door = get_identifier("detach_door");
    linear_names.scope_end_door = get_identifier("scope_end_door");
    linear_names.forward = get_identifier("forward");
    linear_names.sum = get_identifier("sum");
    linear_names.empties = get_identifier("empties");
    linear_names.fills = get_identifier("fills");
    linear_names.tests_full = get_identifier("tests_full");
    linear_names.tests_empty = get_identifier("tests_empty");
    linear_names.needs_full = get_identifier("needs_full");
    linear_names.needs_empty = get_identifier("needs_empty");
    linear_names.once_door = get_identifier("once_door");
    linear_names.moves_own_storage = get_identifier("moves_own_storage");
    if (checks_must_consume) {
        register_callback(plugin_name, PLUGIN_FOLDED_INITIALIZER, on_folded_initializer, nullptr);
        register_callback(plugin_name, PLUGIN_EMPTY_CLASS_ASSIGNMENT, on_empty_class_assignment, nullptr);
    }
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize_linear, nullptr);
    if (checks_shapes) {
        register_callback(plugin_name, PLUGIN_FINISH_TYPE, on_finish_type_linear, nullptr);
        register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_decl_linear, nullptr);
    }
    register_callback(plugin_name, PLUGIN_GGC_START, on_linear_collection, nullptr);
}

}  // namespace crucible_plugin
