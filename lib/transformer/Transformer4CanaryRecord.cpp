#include "Transformer4CanaryRecord.h"

#define POINTER_BIT_SIZE ptrsize*8

#define VOID_TY(m) Type::getVoidTy(m->getContext())
#define INT_N_TY(m,n) Type::getIntNTy(m->getContext(),n)

#define VOID_PTR_TY(m) Type::getInt8PtrTy(m->getContext())
#define THREAD_PTR_TY(m) Type::getIntNPtrTy(m->getContext(), POINTER_BIT_SIZE)
#define INT_TY(m) INT_N_TY(m,32)
#define BOOL_TY(m) INT_N_TY(m, 1)

#define MUTEX_TY(m) m->getTypeByName("union.pthread_mutex_t")
#define MUTEX_ANON_TY(m) m->getTypeByName("struct.anon")
#define CONDN_TY(m) m->getTypeByName("union.pthread_cond_t")

#define MUTEX_PTR_TY(m) PointerType::get(MUTEX_TY(m), 0)
#define CONDN_PTR_TY(m) PointerType::get(CONDN_TY(m), 0)

static bool IS_MUTEX_INIT_TY(Type* t, Module* m) {
    // PTHREAD_MUTEX_INITIALIZER
    // { { i32, i32, i32, i32, i32, { %struct.anon } } }
    if (t->isStructTy()) {
        StructType* st = (StructType*) t;
        if (st->getNumElements() == 1) {
            Type * et = st->getElementType(0);
            if (et->isStructTy()) {
                StructType* est = (StructType*) et;
                if (est->getNumElements() == 6) {
                    for (unsigned i = 0; i < 5; i++) {
                        if (!est->getElementType(i)->isIntegerTy()) {
                            return false;
                        }
                    }

                    Type * sixest = est->getElementType(5);
                    if (sixest->isStructTy()) {
                        StructType* esixest = (StructType*) sixest;
                        if (esixest->getNumElements() == 1 && esixest->getElementType(0) == MUTEX_ANON_TY(m)) {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return true;
}

int Transformer4CanaryRecord::stmt_idx = 0;

Transformer4CanaryRecord::Transformer4CanaryRecord(Module* m, set<Value*>* svs, unsigned psize) : Transformer(m, svs, psize) {
    int idx = 0;
    set<Value*>::iterator it = sharedVariables->begin();
    while (it != sharedVariables->end()) {
        sv_idx_map.insert(pair<Value *, int>(*it, idx++));
        it++;
    }

    ///initialize functions
    F_init = cast<Function>(m->getOrInsertFunction("OnInit",
            VOID_TY(m),
            INT_TY(m),
            NULL));

    F_exit = cast<Function>(m->getOrInsertFunction("OnExit",
            VOID_TY(m),
            NULL));

    F_load = cast<Function>(m->getOrInsertFunction("OnLoad",
            VOID_TY(m),
            INT_TY(m), VOID_PTR_TY(m), INT_TY(m),
            NULL));

    F_prestore = cast<Function>(m->getOrInsertFunction("OnPreStore",
            INT_TY(m),
            INT_TY(m), INT_TY(m),
            NULL));

    F_store = cast<Function>(m->getOrInsertFunction("OnStore",
            VOID_TY(m),
            INT_TY(m), INT_TY(m), INT_TY(m),
            NULL));

    F_prefork = cast<Function>(m->getOrInsertFunction("OnPreFork",
            VOID_TY(m),
            BOOL_TY(m),
            NULL));

    F_fork = cast<Function>(m->getOrInsertFunction("OnFork",
            VOID_TY(m),
            THREAD_PTR_TY(m), BOOL_TY(m),
            NULL));

    if (MUTEX_TY(m) != NULL) {
        F_lock = cast<Function>(m->getOrInsertFunction("OnLock",
                VOID_TY(m),
                MUTEX_PTR_TY(m),
                NULL));

        F_premutexinit = cast<Function>(m->getOrInsertFunction("OnPreMutexInit",
                VOID_TY(m),
                BOOL_TY(m),
                NULL));

        F_mutexinit = cast<Function>(m->getOrInsertFunction("OnMutexInit",
                VOID_TY(m),
                MUTEX_PTR_TY(m), BOOL_TY(m),
                NULL));

        if (CONDN_TY(m) != NULL) {
            F_wait = cast<Function>(m->getOrInsertFunction("OnWait",
                    VOID_TY(m),
                    CONDN_PTR_TY(m), MUTEX_PTR_TY(m),
                    NULL));
        } else {
            errs()<< "********************no pthread_cond_t type\n";
        }
    } else {
        errs()<< "********************no pthread_mutex_t type\n";
    }
}

bool Transformer4CanaryRecord::debug() {
    return false;
}

void Transformer4CanaryRecord::beforeTransform(AliasAnalysis& AA) {
}

void Transformer4CanaryRecord::afterTransform(AliasAnalysis& AA) {
    Function * mainFunction = module->getFunction("main");
    if (mainFunction != NULL) {
        ConstantInt* tmp = ConstantInt::get(INT_TY(module), sharedVariables->size());
        this->insertCallInstAtHead(mainFunction, F_init, tmp, NULL);
        this->insertCallInstAtTail(mainFunction, F_exit, NULL);

        if (MUTEX_ANON_TY(module) != NULL) {
            iplist<GlobalVariable>::iterator git = module->global_begin();
            while (git != module->global_end()) {
                GlobalVariable& gv = *git;
                if (!gv.isThreadLocal() && gv.getType()->getPointerElementType() == MUTEX_TY(module)) {
                    outs() << "Find a global mutex...\n";
                    outs() << gv << "\n";
                    if (IS_MUTEX_INIT_TY(gv.getInitializer()->getType(), module)) {
                        Constant* mutexstar = ConstantExpr::getBitCast(&gv, MUTEX_PTR_TY(module));
                        ConstantInt* tmp = ConstantInt::get(BOOL_TY(module), 0);
                        this->insertCallInstAtHead(mainFunction, F_mutexinit, mutexstar, tmp, NULL);
                    }
                }
                git++;
            }
        } else {
            errs() << "No mutex anon type ... \n";
        }
    } else {
        errs() << "Cannot find main function...\n";
        exit(1);
    }
}

bool Transformer4CanaryRecord::functionToTransform(Function* f) {
    return !f->isIntrinsic() && !f->empty() && !this->isInstrumentationFunction(f);
}

bool Transformer4CanaryRecord::blockToTransform(BasicBlock* bb) {
    return true;
}

bool Transformer4CanaryRecord::instructionToTransform(Instruction* ins) {
    return true;
}

void Transformer4CanaryRecord::transformLoadInst(LoadInst* inst, AliasAnalysis& AA) {
    Value * val = inst->getOperand(0);
    int svIdx = this->getValueIndex(val, AA);
    if (svIdx == -1) return;
    
    CastInst* ci = CastInst::CreateZExtOrBitCast(inst, VOID_PTR_TY(module));
    ci->insertAfter(inst);

    ConstantInt* tmp = ConstantInt::get(INT_TY(module), svIdx);
    ConstantInt* debug_idx = ConstantInt::get(INT_TY(module), stmt_idx++);

    this->insertCallInstAfter(ci, F_load, tmp, ci, debug_idx, NULL);
}

void Transformer4CanaryRecord::transformStoreInst(StoreInst* inst, AliasAnalysis& AA) {
    Value * val = inst->getOperand(0);
    int svIdx = this->getValueIndex(val, AA);
    if (svIdx == -1) return;

    ConstantInt* tmp = ConstantInt::get(INT_TY(module), svIdx);
    ConstantInt* debug_idx = ConstantInt::get(INT_TY(module), stmt_idx++);

    CallInst * ci = this->insertCallInstBefore(inst, F_prestore, tmp, debug_idx, NULL);
    
    this->insertCallInstAfter(inst, F_store, tmp, ci, debug_idx, NULL);
}

void Transformer4CanaryRecord::transformPthreadCreate(CallInst* ins, AliasAnalysis& AA) {
    ConstantInt* tmp = ConstantInt::get(BOOL_TY(module), 0);
    this->insertCallInstBefore(ins, F_prefork, tmp, NULL);
    this->insertCallInstAfter(ins, F_fork, ins->getArgOperand(0), tmp, NULL);
}

void Transformer4CanaryRecord::transformPthreadMutexInit(CallInst* ins, AliasAnalysis& AA) {
    ConstantInt* tmp = ConstantInt::get(BOOL_TY(module), 0);
    this->insertCallInstBefore(ins, F_premutexinit, tmp, NULL);
    this->insertCallInstAfter(ins, F_mutexinit, ins->getArgOperand(0), tmp, NULL);
}

void Transformer4CanaryRecord::transformPthreadMutexLock(CallInst* ins, AliasAnalysis& AA) {
    Value * val = ins->getArgOperand(0);
    int svIdx = this->getValueIndex(val, AA);
    if (svIdx == -1) return;

    this->insertCallInstAfter(ins, F_lock, ins->getArgOperand(0), NULL);
}

void Transformer4CanaryRecord::transformPthreadCondWait(CallInst* ins, AliasAnalysis& AA) {
    Value * val = ins->getArgOperand(0);
    int svIdx = this->getValueIndex(val, AA);
    if (svIdx == -1) return;

    // F_wait need two more arguments, cond*, mutex*
    this->insertCallInstAfter(ins, F_wait, ins->getArgOperand(0), ins->getArgOperand(1), NULL);
}

void Transformer4CanaryRecord::transformPthreadCondTimeWait(CallInst* ins, AliasAnalysis& AA) {
    Value * val = ins->getArgOperand(0);
    int svIdx = this->getValueIndex(val, AA);
    if (svIdx == -1) return;

    // F_wait need two more arguments, cond*, mutex*
    this->insertCallInstAfter(ins, F_wait, ins->getArgOperand(0), ins->getArgOperand(1), NULL);
}

void Transformer4CanaryRecord::transformSystemExit(CallInst* ins, AliasAnalysis& AA) {
    this->insertCallInstBefore(ins, F_exit, NULL);
}

bool Transformer4CanaryRecord::isInstrumentationFunction(Function * called) {
    return called == F_init || called == F_exit
            || called == F_load
            || called == F_prestore || called == F_store
            || called == F_lock
            || called == F_prefork || called == F_fork
            || called == F_wait
            || called == F_premutexinit || called == F_mutexinit;
}

// private functions

int Transformer4CanaryRecord::getValueIndex(Value* v, AliasAnalysis & AA) {
    set<Value*>::iterator it = sharedVariables->begin();
    while (it != sharedVariables->end()) {
        Value * rep = *it;
        if (AA.alias(v, rep) != AliasAnalysis::NoAlias) {
            return sv_idx_map[rep];
        }
        it++;
    }

    return -1;
}