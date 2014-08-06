typedef MVMint32 (*MVMJitFunc)(MVMThreadContext *tc, MVMCompUnit *cu, void * label);

struct MVMJitCode {
    MVMJitFunc     func_ptr;
    size_t             size;
    MVMuint8      *bytecode;
    MVMint16     num_locals;
    MVMStaticFrame      *sf;
    void           **labels;
    MVMint32     num_labels;
    
    void       **osr_labels;
    MVMint32   *osr_offsets;
    MVMint32 num_osr_labels;

    void     **deopt_all_labels;
    MVMint32  *deopt_all_indexes;
    MVMint32   num_deopt_all_labels;
};

MVMJitCode* MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph);
void MVM_jit_destroy_code(MVMThreadContext *tc, MVMJitCode *code);
MVMint32 MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                            MVMJitCode * code);

#define MVM_JIT_CTRL_DEOPT -1
#define MVM_JIT_CTRL_NORMAL 0
