# Generates the typed-IR step 6 goldens (tests/ir/lit_*, mismatch_* .mn/.ir) from a spec, computing every column and message independently of the C checker; run from the repo root.
import sys, os
OUT = "tests/ir"
BITS = {"u8":8,"u16":16,"u32":32,"u64":64,"i8":8,"i16":16,"i32":32,"i64":64}
def signed(t): return t[0]=="i"
def imax(t):
    b=BITS[t]
    return (1<<(b-1))-1 if signed(t) else (1<<b)-1
# expression AST: ("int", text) | ("bool", "true"|"false") | ("paren", inner) | ("neg", inner, spaced)
#                  | ("bin", op, lhs, rhs)
def val(text): return int(text,16) if text.lower().startswith("0x") else int(text)
def src(n):
    if n[0] in ("int","bool"): return n[1]
    if n[0]=="paren": return "("+src(n[1])+")"
    if n[0]=="bin": return src(n[2])+" "+n[1]+" "+src(n[3])
    return "-"+(" " if n[2] else "")+src(n[1])
def isint(T): return T in BITS
def dump(n, col, T, ind, override=None):
    pad="  "*ind
    s=src(n); a,b=override or (col,col+len(s))
    sp="{L}:%d-{L}:%d"%(a,b)
    if n[0]=="int": return [pad+"(int@%s %d %s)"%(sp,val(n[1]),T)]
    if n[0]=="bool": return [pad+"(bool@%s %s bool)"%(sp,n[1])]
    if n[0]=="paren": return dump(n[1],col+1,T,ind,(col,col+len(s)))
    if n[0]=="bin":
        l=dump(n[2],col,T,ind+1); rc=col+len(src(n[2]))+len(n[1])+2
        r=dump(n[3],rc,T,ind+1)
        lines=[pad+"(binary@%s %s %s"%(sp,n[1],T)]+l+r
        lines[-1]+=")"
        return lines
    inner=n[1]
    if inner[0]=="int" and val(inner[1])==1<<(BITS[T]-1):
        return [pad+"(int@%s %d %s)"%(sp,-(1<<(BITS[T]-1)),T)]
    ccol=col+1+(1 if n[2] else 0)
    lines=[pad+"(unary@%s - %s"%(sp,T)]+dump(inner,ccol,T,ind+1)
    lines[-1]+=")"
    return lines
def ttext(T): return T
def errors(n, col, T, start=None):
    at=start if start is not None else col
    if n[0]=="bool":
        return [] if T=="bool" else [(at,"E0223","type mismatch: expected %s, found bool"%T)]
    if not isint(T):
        return [(at,"E0223","type mismatch: expected %s, found integer literal"%T)]
    if n[0]=="int":
        if val(n[1])>imax(T): return [(at,"E0224","integer literal %d does not fit in %s"%(val(n[1]),T))]
        return []
    if n[0]=="paren": return errors(n[1],col+1,T,col)
    if n[0]=="bin":
        return errors(n[2],col,T)+errors(n[3],col+len(src(n[2]))+len(n[1])+2,T)
    if not signed(T): return [(at,"E0260","operator '-' is not defined for %s"%T)]
    inner=n[1]; ccol=col+1+(1 if n[2] else 0)
    if inner[0]=="int":
        half=1<<(BITS[T]-1); v=val(inner[1])
        return [(at,"E0224","integer literal -%d does not fit in %s"%(v,T))] if v>half else []
    return errors(inner,ccol,T)
def build(name, funcs, prelude=()):
    text=list(prelude); L=2+len(prelude); ir=[]; errs=[]
    # struct preludes are printed by the caller's expected text (prelude_ir)
    for (fn,T,node) in funcs:
        e=src(node)
        prefix="fn %s() -> %s { return "%(fn,T)
        line=prefix+e+"; }"
        text.append(line)
        ecol=len(prefix)+1
        errs+= [(L,)+x for x in errors(node,ecol,T)]
        rcol=prefix.index("return")+1
        bcol=prefix.index("{")+1
        end=len(line)+1
        if not errors(node,ecol,T):
            d=["(fn@{L}:1-{L}:%d fn:%s"%(end,fn),"  (params)","  (returns %s)"%T,
               "  (block@{L}:%d-{L}:%d"%(bcol,end),
               "    (return@{L}:%d-{L}:%d"%(rcol,len(prefix)+len(e)+2)]+dump(node,ecol,T,3)
            d[-1]+=")))"
            ir.append([x.replace("{L}",str(L)) for x in d])
        L+=1
    open(os.path.join(OUT,name+".mn"),"w").write("\n".join(["manna v0;"]+text)+"\n")
    out=(["ERRORS:"]+["%d:%d %s %s"%e for e in errs]) if errs else [l for d in ir for l in d]
    open(os.path.join(OUT,name+".ir"),"w").write("\n".join(out)+"\n")
I=lambda t:("int",t)
B=lambda t:("bool",t)
P=lambda n:("paren",n)
N=lambda n,sp=False:("neg",n,sp)
BIN=lambda op,l,r:("bin",op,l,r)
if __name__=="__main__":
    for T in ["i8","i16","i32","i64"]:
        b=BITS[T]; mx=(1<<(b-1))-1; half=1<<(b-1)
        build("lit_%s_ok"%T,[("max",T,I(str(mx))),("min",T,N(I(str(half)))),("neg",T,N(I(str(mx)))),("zero",T,I("0")),("neg_zero",T,N(I("0"))),("paren",T,P(I(str(mx)))),("neg_paren",T,N(P(I(str(mx)))))])
        build("lit_%s_range"%T,[("over",T,I(str(half))),("neg_over",T,N(I(str(half+1)))),("neg_paren_min",T,N(P(I(str(half))))),("far_over",T,I(str(mx*2+1)) if T!="i64" else I("18446744073709551615"))])
    for T in ["u8","u16","u32","u64"]:
        b=BITS[T]; mx=(1<<b)-1
        build("lit_%s_ok"%T,[("max",T,I(str(mx))),("zero",T,I("0")),("paren",T,P(I(str(mx))))])
        if T!="u64":
            build("lit_%s_range"%T,[("over",T,I(str(mx+1))),("paren_over",T,P(I(str(mx+1)))),("far_over",T,I(str(mx*3)))])
    build("lit_unsigned_neg",[("one","u32",N(I("1"))),("zero","u8",N(I("0"))),("min64","u64",N(I("9223372036854775808"))),("paren","u16",N(P(I("1")))),("twice","u8",N(N(I("1")))),("big_literal_silent","u8",N(I("256"))),("spaced","u32",N(I("5"),True))])
    build("lit_hex_i64_min",[("min","i64",N(I("0x8000000000000000"))),("max","i64",I("0x7fffffffffffffff"))])
    build("lit_spaced_neg",[("min","i8",N(I("128"),True))])
    build("lit_double_neg",[("min","i8",N(N(I("128")))),("plain","i16",N(N(I("5"))))])
    build("lit_double_neg_over",[("over","i8",N(N(I("129"))))])
    # ---- 6b ----
    build("lit_binary_ok",[
        ("add","u8",BIN("+",I("1"),I("2"))),
        ("precedence","u8",BIN("+",I("1"),BIN("*",I("2"),I("3")))),
        ("grouped","u8",BIN("*",P(BIN("+",I("1"),I("2"))),I("3"))),
        ("chain","u16",BIN("+",BIN("+",I("1"),I("2")),I("3"))),
        ("bitand","u8",BIN("&",I("255"),I("15"))),
        ("shift","u32",BIN("<<",I("1"),I("31"))),
        ("shift_right","u32",BIN(">>",I("256"),I("4"))),
        ("bitor","u8",BIN("|",I("240"),I("15"))),
        ("bitxor","u8",BIN("^",I("255"),I("15"))),
        ("neg_lhs","i8",BIN("+",N(I("1")),I("5"))),
        ("neg_rhs","i8",BIN("-",I("5"),N(I("3")))),
        ("min_lhs","i64",BIN("+",N(I("9223372036854775808")),I("1"))),
        ("div_mod","u8",BIN("%",BIN("/",I("10"),I("2")),I("3"))),
        ("neg_group","i16",N(P(BIN("+",I("1"),I("2"))))),
        ("paren_min","i8",P(N(I("128")))),
    ])
    build("lit_binary_range",[
        ("left","u8",BIN("+",I("256"),I("1"))),
        ("right","u8",BIN("+",I("1"),I("256"))),
        ("both_reported","u8",BIN("+",I("256"),I("300"))),
        ("nested","u8",BIN("*",P(BIN("+",I("250"),I("10"))),I("300"))),
        ("signed","i8",BIN("+",N(I("129")),N(I("129")))),
    ])
    build("lit_binary_unsigned_neg",[
        ("rhs","u8",BIN("+",I("1"),N(I("1")))),
        ("group","u8",N(P(BIN("+",I("1"),I("2"))))),
        ("both","u16",BIN("-",N(I("1")),N(I("2")))),
    ])
    build("lit_bool_ok",[("yes","bool",B("true")),("no","bool",B("false"))])
    build("mismatch_bool_in_int",[("a","u8",B("true")),("b","i64",B("false"))])
    build("mismatch_int_in_bool",[("number","bool",I("1")),("negated","bool",N(I("1"))),("paren","bool",P(I("1"))),("group","bool",BIN("+",I("1"),BIN("*",I("2"),I("3")))),("neg_group","bool",N(P(BIN("+",I("1"),I("2")))))])
    build("mismatch_no_poison_of_literals",[("bool_expected","bool",BIN("+",I("256"),I("300"))),("int_expected","u8",BIN("+",I("256"),I("300")))])
    build("mismatch_array_return",[("a","[3]u8",I("1")),("b","[3]u8",B("true"))])
