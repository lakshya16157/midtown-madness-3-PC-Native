"""Release CPU benchmark for car local-light production, without a game process.

Run after scripts/enter-env.ps1. The real generated native producer runs with
only flare graphics submission stubbed, as in the car regression. Timings use
uninstrumented /O2 builds; a separate build reports deterministic operation
counts. Temporary executables are removed automatically. --baseline can point
at a saved producer source for an exact historical comparison.
"""
from pathlib import Path
import argparse
import importlib.util
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "native_projector", ROOT / "tests/mm3_headlight_projector_regression.py")
native_projector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native_projector)

REFERENCE_FRAME = r'''static void car_light_frame(void)
{
    uint32_t queue = MEM32(0x003C5D8Cu);
    uint32_t count = queue ? MEM32(queue + 0x5810) : 0;
    uint32_t frame = queue ? MEM32(queue + 0x5ACC) : 0;
    unsigned i, j;
    int changed = queue != s_car_queue || frame != s_car_frame || count < s_car_count;
    for (i = 0; !changed && i != s_car_groups; ++i)
        for (j = 0; j != 2; ++j) {
            const struct car_light_group *group = s_car_group + i;
            if (group->record[j] && memcmp((const void *)XBOX_PTR(group->record[j]),
                                          group->image[j], 0x54)) {
                changed = 1;
                break;
            }
        }
    if (changed) {
        s_car_groups = 0;
        memset(s_car_axis, 0, sizeof s_car_axis);
    }
    s_car_queue = queue;
    s_car_frame = frame;
    s_car_count = count;
}'''


def function(source, name):
    return re.search(r"(?:static )?(?:void|int) " + name +
                     r"\([^)]*\)\n\{.*?^\}", source, re.M | re.S).group()


def replace_function(source, name, replacement):
    return source.replace(function(source, name), replacement, 1)


def reference_costs(source):
    source = replace_function(source, "car_light_frame", REFERENCE_FRAME)
    submit = function(source, "car_light_submit")
    if "    car_light_frame();" not in submit:
        source = source.replace(submit, submit.replace(
            "    if (!s_car_queue) return;", "    car_light_frame();\n    if (!s_car_queue) return;"))
    heading = function(source, "car_light_heading")
    source = source.replace(heading, re.sub(
        r"^\s*if \(!s_car_axis\[at\]\.owner\) break;\n", "", heading, flags=re.M))
    return source


def no_scan(source):
    frame = function(source, "car_light_frame")
    frame = re.sub(r"    for \(i = 0; !changed.*?(?=    if \(changed\))", "", frame, flags=re.S)
    frame = frame.replace("    unsigned i, j;\n", "")
    return replace_function(source, "car_light_frame", frame)


def one_check(source):
    submit = function(source, "car_light_submit")
    return source.replace(submit, submit.replace("    car_light_frame();\n", ""))


def empty_axis_break(source):
    heading = function(source, "car_light_heading")
    if "if (!s_car_axis[at].owner) break;" in heading:
        return source
    return source.replace(heading, heading.replace(
        "        unsigned at = (slot + i) % CAR_AXIS_SLOTS;",
        "        unsigned at = (slot + i) % CAR_AXIS_SLOTS;\n"
        "        if (!s_car_axis[at].owner) break;"))


def instrument(source):
    frame = function(source, "car_light_frame").replace("{\n", "{\n    BENCH_ADD(frame_checks, 1);\n", 1)
    frame = frame.replace("memcmp(", "bench_frame_memcmp(")
    source = replace_function(source, "car_light_frame", frame)
    valid = function(source, "car_light_group_valid").replace("memcmp(", "bench_valid_memcmp(")
    valid = valid.replace(
        "                if (MEM32(slots + j * 4) == group->record[i]) break;",
        "                BENCH_ADD(membership_probes, 1);\n"
        "                if (MEM32(slots + j * 4) == group->record[i]) break;")
    source = replace_function(source, "car_light_group_valid", valid)
    source = source.replace("        unsigned at = (slot + i) % CAR_AXIS_SLOTS;",
                            "        unsigned at = (slot + i) % CAR_AXIS_SLOTS; BENCH_ADD(axis_probes, 1);")
    source = source.replace("        slot = (start + i) % CAR_AXIS_SLOTS;",
                            "        slot = (start + i) % CAR_AXIS_SLOTS; BENCH_ADD(axis_insert_probes, 1);")
    source = source.replace("        struct car_light_group *candidate = s_car_group + i;",
                            "        struct car_light_group *candidate = s_car_group + i; BENCH_ADD(group_probes, 1);")
    source = source.replace("        /* group lookup probe */", "        BENCH_ADD(group_probes, 1);")
    source = source.replace("static void car_light_group_draw(struct car_light_group *group, float radius)\n{",
                            "static void car_light_group_draw(struct car_light_group *group, float radius)\n{\n"
                            "    BENCH_ADD(group_draws, 1);")
    source = source.replace("static void car_light_hook(void)\n{", "static void car_light_hook(void)\n{\n"
                            "    BENCH_ADD(callbacks, 1);")
    source = source.replace("void mm3_car_lighting_emit(uint32_t node, uint32_t mask, const float source[12])\n{",
                            "void mm3_car_lighting_emit(uint32_t node, uint32_t mask, const float source[12])\n{\n"
                            "    BENCH_ADD(emit_calls, 1);")
    source = source.replace("        struct car_light_group *group = s_car_group + i;",
                            "        struct car_light_group *group = s_car_group + i; BENCH_ADD(align_probes, 1);")
    source = source.replace("        group = s_car_group + s_car_groups++;",
                            "        group = s_car_group + s_car_groups++; BENCH_ADD(group_allocations, 1);")
    source = source.replace("if (count >= (cone ? CAR_CONE_CAP : CAR_POINT_CAP)) {",
                            "if (count >= (cone ? CAR_CONE_CAP : CAR_POINT_CAP)) { BENCH_ADD(queue_rejects, 1);")
    source = source.replace("if (!priority || count != CAR_CONE_CAP || !car_light_reclaim(queue, count)) {",
                            "if (!priority || count != CAR_CONE_CAP || !car_light_reclaim(queue, count)) { BENCH_ADD(queue_rejects, 1);")
    return source


COUNTERS = r'''
#include "mm3_light_queue.c"
#include <windows.h>
#include <inttypes.h>
struct bench_operations {
    uint64_t callbacks,frame_checks,frame_images,valid_images;
    uint64_t membership_probes,axis_probes,group_probes,group_draws;
    uint64_t axis_insert_probes,align_probes,group_allocations,queue_rejects;
    uint64_t emit_calls;
};
static struct bench_operations bench_ops;
#if BENCH_COUNT_OPS
#define BENCH_ADD(field, amount) (bench_ops.field += (amount))
static int bench_frame_memcmp(const void *a,const void *b,size_t n)
{ ++bench_ops.frame_images; return memcmp(a,b,n); }
static int bench_valid_memcmp(const void *a,const void *b,size_t n)
{ ++bench_ops.valid_images; return memcmp(a,b,n); }
#else
#define BENCH_ADD(field, amount) ((void)0)
#define bench_frame_memcmp memcmp
#define bench_valid_memcmp memcmp
#endif
'''

BENCH = r'''
enum { BENCH_NODES=0x200000, BENCH_OWNERS=0x100000, BENCH_MATRICES=0x500000 };
static volatile uint32_t bench_sink;
static unsigned bench_cars,bench_masked,bench_env;
static void prepare(unsigned cars,unsigned masked,unsigned env)
{
    unsigned car,lamp,i;
    const unsigned kinds[13]={0,0,1,1,2,2,3,3,5,5,6,6,8};
    bench_cars=cars;bench_masked=masked;bench_env=env;
    memset(memory,0,sizeof memory);g_xbox_mem_offset=(ptrdiff_t)memory;
    native_constants();s_dbg_on=0;
    MEM32(0x3c55a0)=WORLD;MEM32(WORLD+0x24c)=0x1234;
    MEM32(0x3c5d8c)=QUEUE;MEM32(0x407c30)=3;
    for(i=0;i<256;++i) MEM32(QUEUE+0x5410+i*4)=RECORD+i*0x54;
    for(car=0;car<cars;++car) {
        uint32_t owner=BENCH_OWNERS+car*0x400;
        MEM8(owner+0xf3)=1;
        for(lamp=0;lamp<13;++lamp) {
            uint32_t node=BENCH_NODES+car*0x1000+lamp*0x90;
            uint32_t matrix=BENCH_MATRICES+car*0x400+lamp*0x30;
            float toe=lamp<2 ? (lamp?.3f:-.3f) : 0;
            MEM32(node+4)=owner;MEM32(node+0x34)=kinds[lamp];
            MEM32(node+0x64)=kinds[lamp]==1?0xffff0000u:0xffffffffu;
            MEMF(node+0x54)=MEMF(node+0x78)=.5f;
            MEMF(node+0x5c)=MEMF(node+0x7c)=.03f;
            MEMF(matrix)=cosf(toe);MEMF(matrix+8)=-sinf(toe);
            MEMF(matrix+0x10)=1;
            MEMF(matrix+0x18)=sinf(toe);MEMF(matrix+0x20)=cosf(toe);
            MEMF(matrix+0x24)=car*4+(lamp&1?-.7f:.7f);
            MEMF(matrix+0x28)=.7f;MEMF(matrix+0x2c)=car*2;
        }
    }
}
static void callback(unsigned car,unsigned lamp)
{
    g_ecx=BENCH_NODES+car*0x1000+lamp*0x90;g_esp=STACK;
    MEM32(STACK+4)=BENCH_MATRICES+car*0x400+lamp*0x30;
    MEM32(STACK+8)=0;MEM32(STACK+12)=0x103;
    sub_000CF2DA();
}
#if BENCH_PREPASS && BENCH_HAS_EMIT
static void prepass(unsigned car,unsigned lamp)
{
    uint32_t node=BENCH_NODES+car*0x1000+lamp*0x90;
    uint32_t matrix=BENCH_MATRICES+car*0x400+lamp*0x30;
    mm3_car_lighting_emit(node,0x103,(const float *)XBOX_PTR(matrix));
}
#endif
static void frame(void)
{
    unsigned car,lamp,pass,i;
#if BENCH_HAS_RESET_HOOK
    g_ecx=QUEUE;g_esp=STACK;sub_0022FDFD();
#else
    g_ecx=QUEUE;g_esp=STACK;sub_0022FDFD_original();
#endif
    /* 51 native street/scene records coexist with car fields. Pointer order
     * is retained across frames, as in the native reusable record pool. */
    for(i=0;i<51;++i) {
        uint32_t record=MEM32(QUEUE+0x5410+i*4);
        memset((void *)XBOX_PTR(record),0,0x54);
        MEMF(record)=MEMF(record+0x14)=MEMF(record+0x28)=1;
        MEM32(record+0x4c)=0xff102030;MEM32(record+0x50)=0;
    }
    MEM32(QUEUE+0x5810)=51;
    for(car=0;car<bench_cars;++car) {
#if BENCH_PREPASS && BENCH_HAS_EMIT
        /* The pre-cull walker emits a model's local sources; a visible body
         * immediately traverses those same nodes through the native hook. */
        for(lamp=0;lamp<12;++lamp) prepass(car,lamp);
        if(bench_env) prepass(car,12);
#endif
        for(lamp=0;lamp<4;++lamp) callback(car,lamp);
        for(pass=0;pass<bench_masked;++pass) callback(car,4+pass%8);
        if(bench_env) callback(car,12);
    }
    bench_sink^=MEM32(QUEUE+0x5810);
}
static int compare_double(const void *a,const void *b)
{ double x=*(const double *)a,y=*(const double *)b; return (x>y)-(x<y); }
int main(void)
{
    const unsigned cases[][3]={{20,8,1},{80,8,1},{20,32,1},{80,32,1},{80,8,0}
#if BENCH_STRESS
        ,{160,8,1},{256,8,1},{320,8,1},{160,32,1},{320,32,1},{320,8,0}
#endif
    };
    LARGE_INTEGER frequency,start,finish;
    unsigned scenario,sample,i,frames;
    double samples[7];
    struct bench_operations original_operations[2];
    QueryPerformanceFrequency(&frequency);
    for(scenario=0;scenario<sizeof cases/sizeof cases[0];++scenario) {
        prepare(cases[scenario][0],cases[scenario][1],cases[scenario][2]);
        for(i=0;i<50;++i) frame();
#if BENCH_COUNT_OPS
        memset(&bench_ops,0,sizeof bench_ops);frame();
#if BENCH_EXPECT_BOUNDED
        if(bench_ops.frame_images ||
           bench_ops.frame_checks!=(bench_ops.emit_calls?bench_ops.emit_calls:bench_ops.callbacks) ||
           (bench_cars<=CAR_AXIS_SLOTS &&
            bench_ops.axis_probes>(bench_ops.emit_calls?bench_ops.emit_calls:bench_ops.callbacks))) {
            fputs("FAIL: producer repeated a global image scan/frame check or unbounded axis miss\n",stderr);
            return 2;
        }
        if(scenario<2) original_operations[scenario]=bench_ops;
        if(scenario==2 || scenario==3) {
            const struct bench_operations *original=original_operations+scenario-2;
            if(bench_ops.valid_images!=original->valid_images ||
               bench_ops.membership_probes!=original->membership_probes ||
               bench_ops.axis_probes!=original->axis_probes ||
               bench_ops.group_probes!=original->group_probes ||
               bench_ops.group_draws!=original->group_draws) {
                fputs("FAIL: masked callbacks increased car-cache or queue validation work\n",stderr);
                return 3;
            }
        }
#endif
        printf("OPS cars=%u masked=%u env=%u prepass=%u callbacks=%" PRIu64 " emit_calls=%" PRIu64 " frame_checks=%" PRIu64
               " frame_images=%" PRIu64 " frame_image_bytes=%" PRIu64 " valid_images=%" PRIu64
               " membership_probes=%" PRIu64 " axis_probes=%" PRIu64
               " group_probes=%" PRIu64 " group_draws=%" PRIu64
               " axis_insert_probes=%" PRIu64 " align_probes=%" PRIu64
               " group_allocations=%" PRIu64 " queue_rejects=%" PRIu64 "\n",
               bench_cars,bench_masked,bench_env,BENCH_PREPASS,bench_ops.callbacks,bench_ops.emit_calls,bench_ops.frame_checks,
               bench_ops.frame_images,bench_ops.frame_images*84,bench_ops.valid_images,
               bench_ops.membership_probes,bench_ops.axis_probes,bench_ops.group_probes,bench_ops.group_draws,
               bench_ops.axis_insert_probes,bench_ops.align_probes,bench_ops.group_allocations,bench_ops.queue_rejects);
        {
            unsigned empty=0,duplicates=0,j,k;
            for(j=0;j<s_car_groups;++j) {
                const struct car_light_group *group=s_car_group+j;
                if(!group->record[0] && !group->record[1]) ++empty;
                for(k=0;k<j;++k) if(s_car_group[k].owner==group->owner &&
                    s_car_group[k].kind==group->kind && s_car_group[k].color==group->color) {
                    ++duplicates;break;
                }
            }
#if BENCH_EXPECT_GROUP_HASH
            if(empty || duplicates || s_car_groups>CAR_GROUP_SLOTS || MEM32(QUEUE+0x5810)>256) {
                fputs("FAIL: queue pressure created empty/duplicate groups or exceeded native capacity\n",stderr);
                return 4;
            }
#endif
#if BENCH_PREPASS
            if(MEM32(QUEUE+0x5810)!=(bench_cars==20?111u:bench_cars==80?246u:256u)) {
                fputs("FAIL: pre-cull and visible emission duplicated native light records\n",stderr);
                return 5;
            }
#endif
            printf("CAPACITY cars=%u masked=%u env=%u records=%u groups=%u empty_groups=%u duplicate_groups=%u\n",
                   bench_cars,bench_masked,bench_env,MEM32(QUEUE+0x5810),s_car_groups,empty,duplicates);
        }
#else
        frames=bench_cars<=20?1000:bench_cars<=80?400:160;
        for(sample=0;sample<7;++sample) {
            QueryPerformanceCounter(&start);
            for(i=0;i<frames;++i) frame();
            QueryPerformanceCounter(&finish);
            samples[sample]=(finish.QuadPart-start.QuadPart)*1000.0/frequency.QuadPart/frames;
        }
        qsort(samples,7,sizeof samples[0],compare_double);
        printf("TIME cars=%u masked=%u env=%u prepass=%u median_ms=%.6f min_ms=%.6f max_ms=%.6f\n",
               bench_cars,bench_masked,bench_env,BENCH_PREPASS,samples[3],samples[0],samples[6]);
#endif
        fflush(stdout);
    }
    return bench_sink==0xffffffffu;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--counts-only", action="store_true")
    parser.add_argument("--stress", action="store_true", help="Include 160/256/320-car queue and cache saturation")
    parser.add_argument("--prepass", action="store_true", help="Emit pre-cull sources before their visible native callbacks")
    parser.add_argument("--variants", nargs="+", default=["baseline", "no_scan", "one_check", "axis_break", "combined", "current"])
    args = parser.parse_args()
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    current = (ROOT / "src/mm3_car_lighting.c").read_text()
    baseline = args.baseline.read_text() if args.baseline else reference_costs(current)
    variants = {"baseline": baseline, "no_scan": no_scan(baseline),
                "one_check": one_check(baseline), "axis_break": empty_axis_break(baseline),
                "combined": empty_axis_break(one_check(no_scan(baseline))), "current": current}
    native_projector.NATIVE += ("sub_0022FDFD", "sub_000DFE0E")
    native = native_projector.generated().replace(
        "void sub_000CF2DA(void)", "void sub_000CF2DA_original(void)")
    native = native.replace("void sub_0022FDFD(void)", "void sub_0022FDFD_original(void)")
    native = native.replace("void sub_000DFE0E(void)", "void sub_000DFE0E_original(void)")
    prefix = native_projector.SOURCE.split("static unsigned submit", 1)[0]
    prefix = prefix.replace("memory[0x400000]", "memory[0x600000]")
    prefix = prefix.replace('void sub_001A842A(void) { assert(!"unexpected flare submission"); }',
                            'void sub_001A842A(void) { g_esp+=16; }')
    with tempfile.TemporaryDirectory(prefix="mm3-car-performance-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        declarations = "\n".join(f"void {name}(void);" for name in native_projector.NATIVE)
        declarations += "\nvoid sub_000CF2DA_original(void);\nvoid sub_000DFE0E_original(void);\n"
        (folder / "native.inc").write_text(declarations + native)
        (folder / "constants.inc").write_text(native_projector.constants(native))
        for name in args.variants:
            producer = variants[name]
            source = prefix + COUNTERS + instrument(producer) + BENCH
            (folder / "benchmark.c").write_text(source)
            has_reset = int("void sub_0022FDFD(void)" in producer)
            has_group_hash = int("car_light_group_bucket(" in producer)
            has_emit = int("void mm3_car_lighting_emit(" in producer)
            if args.prepass and not has_emit:
                raise SystemExit(f"{name} has no source-only emitter for --prepass")
            bounded = int(has_reset and "memcmp(" not in function(producer, "car_light_frame")
                          and "    car_light_frame();" not in function(producer, "car_light_submit"))
            for counted in ([1] if args.counts_only else [0, 1]):
                executable = folder / f"{name}-{counted}.exe"
                result = subprocess.run(["cl", "/nologo", "/W3", "/O2", "/DNDEBUG", "/std:c11",
                                f"/DBENCH_COUNT_OPS={counted}", f"/DBENCH_HAS_RESET_HOOK={has_reset}",
                                f"/DBENCH_EXPECT_BOUNDED={bounded}",
                                f"/DBENCH_STRESS={int(args.stress)}",
                                f"/DBENCH_EXPECT_GROUP_HASH={has_group_hash}",
                                f"/DBENCH_PREPASS={int(args.prepass)}", f"/DBENCH_HAS_EMIT={has_emit}",
                                "/I" + str(folder), "/I" + str(ROOT / "src"),
                                "/I" + str(ROOT / "tools/xboxrecomp/include"),
                                "/Fo:" + str(folder / "benchmark.obj"), "/Fe:" + str(executable),
                                str(folder / "benchmark.c")], cwd=folder,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                if result.returncode:
                    print(result.stdout)
                    result.check_returncode()
                print(f"VARIANT {name} {'operations' if counted else 'release timings'}", flush=True)
                subprocess.run([str(executable)], check=True, cwd=folder)


if __name__ == "__main__":
    main()
