#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Bytecode/Types.h>

class FunctionImpl {
public:
    static ObjClass *Class;
};
#endif

#include <Engine/Bytecode/TypeImpl/FunctionImpl.h>
#include <Engine/Bytecode/BytecodeObjectManager.h>
#include <Engine/Bytecode/StandardLibrary.h>

ObjClass* FunctionImpl::Class = nullptr;

PUBLIC STATIC void FunctionImpl::Init() {
    const char *name = "$$FunctionImpl";

    Class = NewClass(Murmur::EncryptString(name));
    Class->Name = CopyString(name);

    BytecodeObjectManager::DefineNative(Class, "bind", FunctionImpl::VM_Bind);

    BytecodeObjectManager::ClassImplList.push_back(Class);
}

#define GET_ARG(argIndex, argFunction) (StandardLibrary::argFunction(args, argIndex, threadID))

PUBLIC STATIC VMValue FunctionImpl::VM_Bind(int argCount, VMValue* args, Uint32 threadID) {
    StandardLibrary::CheckArgCount(argCount, 2);

    ObjFunction* function = GET_ARG(0, GetFunction);

    return OBJECT_VAL(NewBoundMethod(args[1], function));
}
