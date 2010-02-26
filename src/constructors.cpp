#include "clay.hpp"

vector<OverloadPtr> typeOverloads;

void addTypeOverload(OverloadPtr x)
{
    typeOverloads.insert(typeOverloads.begin(), x);
}

void initTypeOverloads(TypePtr t)
{
    assert(!t->overloadsInitialized);

    for (unsigned i = 0; i < typeOverloads.size(); ++i) {
        OverloadPtr x = typeOverloads[i];
        EnvPtr env = new Env(x->env);
        const vector<IdentifierPtr> &pvars = x->code->patternVars;
        for (unsigned i = 0; i < pvars.size(); ++i) {
            PatternCellPtr cell = new PatternCell(pvars[i], NULL);
            addLocal(env, pvars[i], cell.ptr());
        }
        PatternPtr pattern = evaluatePattern(x->target, env);
        if (unify(pattern, t.ptr()))
            t->overloads.insert(t->overloads.begin(), x);
    }

    t->overloadsInitialized = true;
}

void initBuiltinIsStaticFlags(TypePtr t) {
    switch (t->typeKind) {

    case ARRAY_TYPE : {
        ArrayType *x = (ArrayType *)t.ptr();
        FlagsMapEntry &flags = lookupFlagsMapEntry(t.ptr(), x->size);
        assert(!flags.initialized);
        flags.initialized = true;
        flags.isStaticFlags.assign(x->size, false);
        break;
    }

    case TUPLE_TYPE : {
        TupleType *x = (TupleType *)t.ptr();
        FlagsMapEntry &flags =
            lookupFlagsMapEntry(t.ptr(), x->elementTypes.size());
        assert(!flags.initialized);
        flags.initialized = true;
        flags.isStaticFlags.assign(x->elementTypes.size(), false);
        break;
    }

    case RECORD_TYPE : {
        RecordType *x = (RecordType *)t.ptr();
        const vector<TypePtr> &fieldTypes = recordFieldTypes(x);
        FlagsMapEntry &flags = lookupFlagsMapEntry(t.ptr(), fieldTypes.size());
        assert(!flags.initialized);
        flags.initialized = true;
        flags.isStaticFlags.assign(fieldTypes.size(), false);
        break;
    }

    }
}

void verifyBuiltinConstructor(TypePtr t,
                              const vector<bool> &isStaticFlags,
                              const vector<ObjectPtr> &argsKey,
                              const vector<LocationPtr> &argLocations)
{
    switch (t->typeKind) {

    case ARRAY_TYPE : {
        ArrayType *x = (ArrayType *)t.ptr();
        if (x->size != (int)isStaticFlags.size())
            error("incorrect no. of arguments");
        for (unsigned i = 0; i < isStaticFlags.size(); ++i) {
            assert(isStaticFlags[i]);
            assert(argsKey[i]->objKind == TYPE);
            TypePtr y = (Type *)argsKey[i].ptr();
            if (y != x->elementType) {
                LocationContext loc(argLocations[i]);
                error("type mismatch");
            }
        }
        break;
    }

    case TUPLE_TYPE : {
        TupleType *x = (TupleType *)t.ptr();
        if (x->elementTypes.size() != isStaticFlags.size())
            error("incorrect no. of arguments");
        for (unsigned i = 0; i < isStaticFlags.size(); ++i) {
            assert(isStaticFlags[i]);
            assert(argsKey[i]->objKind == TYPE);
            TypePtr y = (Type *)argsKey[i].ptr();
            if (y != x->elementTypes[i]) {
                LocationContext loc(argLocations[i]);
                error("type mismatch");
            }
        }
        break;
    }

    case RECORD_TYPE : {
        RecordType *x = (RecordType *)t.ptr();
        const vector<TypePtr> &fieldTypes = recordFieldTypes(x);
        if (fieldTypes.size() != isStaticFlags.size())
            error("incorrect no. of arguments");
        for (unsigned i = 0; i < isStaticFlags.size(); ++i) {
            assert(!isStaticFlags[i]);
            assert(argsKey[i]->objKind == TYPE);
            TypePtr y = (Type *)argsKey[i].ptr();
            if (y != fieldTypes[i]) {
                LocationContext loc(argLocations[i]);
                error("type mismatch");
            }
        }
        break;
    }

    default :
        error("no matching constructor found");

    }
}
