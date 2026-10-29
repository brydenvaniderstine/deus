# Generates the typed-IR step 22d goldens (tests/ir/layout_*): round-4 finding 4, decided by Bryden (2026-10-29): REPORT IT. A field whose own type is too large is always
# reported (E0222, at that field's type), whatever else is wrong in the struct: an unknown type, a duplicate field, a cycle. A total-size overflow is reported only up to the
# first field whose size is unknown (the offsets after it are not known). The intended diagnostics are written here from the mistakes, never read from the builder; columns
# come from the source text. Every program holds two or more independent mistakes, so each has a `.mistakes` file. Run from the repo root with PYTHONPATH=tools.
import gen_ir_goldens_step7 as g
import gen_ir_goldens_step12 as s12
import gen_ir_goldens_step21 as s21

e = s12.e
big = "type is too large (size exceeds the limit)"
unk = lambda n: "unknown type '%s'" % n


def golden(name, L, errs, mistakes=None):
    s21.golden(name, L, errs, mistakes)


if __name__ == "__main__":
    tail = ["fn main() {", "}"]
    # the auditor's four (finding 4): the oversized field is reported beside the unrelated mistake, before or after it
    L = ["manna v0;", "struct S { a: Nope, b: [4294967296]u8, }"] + tail
    golden("layout_oversized_field_after_an_unknown_type_is_reported", L, [e(L, 2, "Nope", "E0220", unk("Nope")), e(L, 2, "[4294967296]u8", "E0222", big)])
    L = ["manna v0;", "struct S { a: [4294967296]u8, b: Nope, }"] + tail
    golden("layout_oversized_field_before_an_unknown_type_is_reported", L, [e(L, 2, "Nope", "E0220", unk("Nope")), e(L, 2, "[4294967296]u8", "E0222", big)])
    L = ["manna v0;", "struct S { a: u8, a: Nope, b: [4294967296]u8, }"] + tail
    golden("layout_oversized_field_beside_a_duplicate_field_with_an_unknown_type_is_reported", L,
           [e(L, 2, "a", "E0211", "duplicate field 'a' in struct 'S'", 1), e(L, 2, "Nope", "E0220", unk("Nope")), e(L, 2, "[4294967296]u8", "E0222", big)])
    L = ["manna v0;", "struct A { a: A, b: [4294967296]u8, }"] + tail
    golden("layout_oversized_field_of_a_struct_that_contains_itself_is_reported", L,
           [e(L, 2, "A", "E0221", "struct 'A' contains itself"), e(L, 2, "[4294967296]u8", "E0222", big)])
    # sweep of the same pattern inside layout: a repeated field is not a field, but its type is still a type that can be too large (it is resolved for its own errors)
    L = ["manna v0;", "struct S { a: u8, a: [4294967296]u8, }"] + tail
    golden("layout_oversized_type_of_a_duplicate_field_is_reported", L, [e(L, 2, "a", "E0211", "duplicate field 'a' in struct 'S'", 1), e(L, 2, "[4294967296]u8", "E0222", big)])
    # every oversized field is its own mistake (the first used to end the look at the others)
    L = ["manna v0;", "struct S { a: [4294967296]u8, b: u8, c: [4294967296]u16, }"] + tail
    golden("layout_every_oversized_field_is_reported", L, [e(L, 2, "[4294967296]u8", "E0222", big), e(L, 2, "[4294967296]u16", "E0222", big)])
    # controls: a total that overflows only AFTER a field whose size is unknown is not reported (the offsets after it are not known): one mistake
    L = ["manna v0;", "struct S { a: Nope, b: [2147483647]u8, c: u8, }"] + tail
    golden("layout_total_overflow_after_an_unknown_field_is_not_reported", L, [e(L, 2, "Nope", "E0220", unk("Nope"))])
    # ... and one that overflows BEFORE the unknown field is still reported, at the struct
    L = ["manna v0;", "struct S { b: [2147483647]u8, c: u8, a: Nope, }"] + tail
    golden("layout_total_overflow_before_an_unknown_field_is_reported", L, [e(L, 2, "Nope", "E0220", unk("Nope")), e(L, 2, "S", "E0222", big)])
    # a struct whose REPEATED field has an unknown type has no layout (that field is not kept, but its type failed): a struct that holds it by value says nothing about its size, so
    # T is not claimed too large (one E0211 and one E0220; no E0222). Were S laid out from its one kept field, [4294967296]S would be 4 GiB and T would be reported.
    L = ["manna v0;", "struct S { a: u8, a: Nope, }", "struct T { s: [4294967296]S, }"] + tail
    golden("layout_dependents_of_a_struct_with_a_failed_repeated_field_stay_silent", L,
           [e(L, 2, "a", "E0211", "duplicate field 'a' in struct 'S'", 1), e(L, 2, "Nope", "E0220", unk("Nope"))])
