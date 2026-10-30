# Generates the typed-IR step 22d goldens (tests/ir/layout_*): round-4 finding 4, decided by Bryden (2026-10-29): REPORT IT. A field whose own type is too large is always
# reported (E0222, at that field's type), whatever else is wrong in the struct: an unknown type, a duplicate field, a cycle. The struct TOTAL is a lower bound (Bryden's STOP 2 of
# step C2c, restored 2026-10-30 after the first wording of this rule): a field of unknown size counts as 0 and the check goes on, so known fields that alone exceed the limit are
# reported at the struct; after a field whose own type is too large the total is not checked. The intended diagnostics are written here from the mistakes, never read from the
# builder; columns come from the source text. Every program holds two or more independent mistakes, so each has a `.mistakes` file. Run from the repo root with PYTHONPATH=tools.
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
    # the struct total is a LOWER bound (Bryden's STOP 2 of step C2c, restored 2026-10-30): an unknown field counts as 0 and the check goes on past it, so known fields that alone exceed the
    # limit are a certain mistake whatever the unknown field is: E0222 at the struct's name
    L = ["manna v0;", "struct S { a: Nope, b: [2147483647]u8, c: u8, }"] + tail
    golden("layout_total_overflow_after_an_unknown_field_is_reported", L, [e(L, 2, "Nope", "E0220", unk("Nope")), e(L, 2, "S", "E0222", big)])
    L = ["manna v0;", "struct S { b: [2147483647]u8, c: u8, a: Nope, }"] + tail
    golden("layout_total_overflow_before_an_unknown_field_is_reported", L, [e(L, 2, "Nope", "E0220", unk("Nope")), e(L, 2, "S", "E0222", big)])
    # the honest control: the known fields fit, so an overflow that would depend on the unknown field's size is never claimed
    L = ["manna v0;", "struct S { a: Nope, b: [2147483640]u8, }"] + tail
    golden("layout_known_fields_that_fit_beside_an_unknown_one_are_not_reported", L, [e(L, 2, "Nope", "E0220", unk("Nope"))])
    # after a field whose OWN type is too large the total is not checked any more: that overflow is the field already reported (one E0222, at the field)
    L = ["manna v0;", "struct S { a: [4294967296]u8, b: [2147483647]u8, c: u8, }"] + tail
    golden("layout_total_after_an_oversized_field_is_not_checked", L, [e(L, 2, "[4294967296]u8", "E0222", big)])

    # a struct whose REPEATED field has an unknown type has no layout (that field is not kept, but its type failed): a struct that holds it by value says nothing about its size, so
    # T is not claimed too large (one E0211 and one E0220; no E0222). Were S laid out from its one kept field, [4294967296]S would be 4 GiB and T would be reported.
    L = ["manna v0;", "struct S { a: u8, a: Nope, }", "struct T { s: [4294967296]S, }"] + tail
    golden("layout_dependents_of_a_struct_with_a_failed_repeated_field_stay_silent", L,
           [e(L, 2, "a", "E0211", "duplicate field 'a' in struct 'S'", 1), e(L, 2, "Nope", "E0220", unk("Nope"))])
