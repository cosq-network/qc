#include "qc/type.h"
#include "qc/common.h"
#include <cassert>
#include <sstream>

namespace qc {

// ============================================================
// Type::isInteger / isFloat
// ============================================================
bool Type::isInteger() const {
    switch (kind()) {
    case TypeKind::Bool:
    case TypeKind::Char: case TypeKind::SChar: case TypeKind::UChar:
    case TypeKind::Short: case TypeKind::UShort:
    case TypeKind::Int: case TypeKind::UInt:
    case TypeKind::Long: case TypeKind::ULong:
    case TypeKind::LongLong: case TypeKind::ULongLong:
    case TypeKind::Enum:
        return true;
    default:
        return false;
    }
}

bool Type::isSigned() const {
    switch (kind()) {
    case TypeKind::Char: case TypeKind::SChar:
    case TypeKind::Short:
    case TypeKind::Int:
    case TypeKind::Long:
    case TypeKind::LongLong:
    case TypeKind::Enum:
        return true;
    default:
        return false;
    }
}

bool Type::isFloat() const {
    switch (kind()) {
    case TypeKind::Float: case TypeKind::Double: case TypeKind::LongDouble:
        return true;
    default:
        return false;
    }
}

// ============================================================
// BuiltinType
// ============================================================
u32 BuiltinType::size() const {
    switch (kind_) {
    case TypeKind::Void:       return 0;
    case TypeKind::Bool:       return 1;
    case TypeKind::Char:       return 1;
    case TypeKind::SChar:      return 1;
    case TypeKind::UChar:      return 1;
    case TypeKind::Short:      return 2;
    case TypeKind::UShort:     return 2;
    case TypeKind::Int:        return 4;
    case TypeKind::UInt:       return 4;
    case TypeKind::Long:       return 8;   // LP64
    case TypeKind::ULong:      return 8;   // LP64
    case TypeKind::LongLong:   return 8;
    case TypeKind::ULongLong:  return 8;
    case TypeKind::Float:      return 4;
    case TypeKind::Double:     return 8;
    case TypeKind::LongDouble: return 16;
    case TypeKind::NullptrT:   return 8;
    default:                   return 0;
    }
}

std::string BuiltinType::toString() const {
    switch (kind_) {
    case TypeKind::Void:       return "void";
    case TypeKind::Bool:       return "_Bool";
    case TypeKind::Char:       return "char";
    case TypeKind::SChar:      return "signed char";
    case TypeKind::UChar:      return "unsigned char";
    case TypeKind::Short:      return "short";
    case TypeKind::UShort:     return "unsigned short";
    case TypeKind::Int:        return "int";
    case TypeKind::UInt:       return "unsigned int";
    case TypeKind::Long:       return "long";
    case TypeKind::ULong:      return "unsigned long";
    case TypeKind::LongLong:   return "long long";
    case TypeKind::ULongLong:  return "unsigned long long";
    case TypeKind::Float:      return "float";
    case TypeKind::Double:     return "double";
    case TypeKind::LongDouble: return "long double";
    case TypeKind::NullptrT:   return "nullptr_t";
    default:                   return "<builtin>";
    }
}

// ============================================================
// PointerType
// ============================================================
std::string PointerType::toString() const {
    std::string base = pointee_ ? pointee_->toString() : "<null>";
    switch (kind_) {
    case TypeKind::Pointer:    return base + " *";
    case TypeKind::Reference:  return base + " &";
    case TypeKind::RValueRef:  return base + " &&";
    default:                   return base + " *";
    }
}

// ============================================================
// ArrayType
// ============================================================
std::string ArrayType::toString() const {
    std::string elemStr = elem_ ? elem_->toString() : "<null>";
    if (count_ < 0) {
        return elemStr + "[]";
    }
    return elemStr + "[" + std::to_string(count_) + "]";
}

// ============================================================
// FunctionType
// ============================================================
std::string FunctionType::toString() const {
    std::string s = ret_ ? ret_->toString() : "void";
    s += "(";
    bool first = true;
    for (const auto& p : params_) {
        if (!first) s += ", ";
        first = false;
        s += p.type ? p.type->toString() : "<null>";
        if (!p.name.empty()) {
            s += " ";
            s += p.name;
        }
    }
    if (variadic_) {
        if (!first) s += ", ";
        s += "...";
    }
    s += ")";
    return s;
}

// ============================================================
// RecordType
// ============================================================
std::string RecordType::toString() const {
    std::string prefix;
    switch (kind_) {
    case TypeKind::Struct: prefix = "struct "; break;
    case TypeKind::Union:  prefix = "union ";  break;
    case TypeKind::Class:  prefix = "class ";  break;
    default:               prefix = "record "; break;
    }
    return prefix + name_;
}

void RecordType::setBaseClass(Ref<RecordType> base) {
    base_ = std::move(base);
    if (base_ && kind_ != TypeKind::Union) {
        // Inherit fields immediately to set correct starting offset for new fields
        for (const auto& f : base_->fields()) {
            // We bypass addField logic and just copy because base is already laid out
            fields_.push_back(f);
            size_  = std::max(size_, f.offset + (f.type ? f.type->size() : 0));
            align_ = std::max(align_, f.type ? f.type->align() : 1);
        }
        // Inherit VTable
        vtable_ = base_->vtable();
    }
}

void RecordType::addField(FieldInfo f) {
    u32 fieldAlign = f.type ? f.type->align() : 1;
    if (fieldAlign < 1) fieldAlign = 1;

    if (kind_ == TypeKind::Union) {
        // All union fields are at offset 0
        f.offset = 0;
    } else {
        // Struct: align current size to field alignment
        u32 cur = size_;
        u32 rem = cur % fieldAlign;
        if (rem != 0) {
            cur += fieldAlign - rem;
        }
        f.offset = cur;
        u32 fieldSize = f.type ? f.type->size() : 0;
        size_ = cur + fieldSize;
    }

    // Update struct/union overall alignment
    if (fieldAlign > align_) {
        align_ = fieldAlign;
    }

    fields_.push_back(std::move(f));
}

void RecordType::addMethod(MethodInfo m) {
    m.parent = this;
    methods_.push_back(std::move(m));
}

void RecordType::finalize() {
    if (kind_ == TypeKind::Union) {
        // Union size = max field size
        u32 maxSz = 0;
        for (const auto& f : fields_) {
            u32 fsz = f.type ? f.type->size() : 0;
            if (fsz > maxSz) maxSz = fsz;
        }
        size_ = maxSz;
    } else {
        // C++: Handle virtual functions
        bool hasVirtual = !vtable_.empty();
        for (const auto& m : methods_) if (m.isVirtual) hasVirtual = true;

        if (hasVirtual) {
            bool hasBaseVptr = false;
            if (base_) {
                for (const auto& f : base_->fields()) if (f.name == "_vptr") hasBaseVptr = true;
            }

            if (!hasBaseVptr) {
                // Prepend _vptr field
                FieldInfo vptr;
                vptr.name = "_vptr";
                // Hack: use void* for _vptr
                vptr.type = std::make_shared<PointerType>(std::make_shared<BuiltinType>(TypeKind::Void));
                
                // Shift all existing fields!
                u32 ptrSize = vptr.type->size();
                u32 ptrAlign = vptr.type->align();
                
                vptr.offset = 0;
                for (auto& f : fields_) f.offset += ptrSize;
                fields_.insert(fields_.begin(), vptr);
                
                size_ += ptrSize;
                if (ptrAlign > align_) align_ = ptrAlign;
            }

            // Update VTable
            for (const auto& m : methods_) {
                if (!m.isVirtual) continue;
                bool overridden = false;
                for (auto& ve : vtable_) {
                    if (ve.method->name == m.name) {
                        ve.method = &m;
                        overridden = true;
                        break;
                    }
                }
                if (!overridden) {
                    vtable_.push_back({&m, (int)vtable_.size()});
                }
            }
        }
    }

    // Pad struct to its alignment boundary
    if (align_ > 0 && size_ % align_ != 0) {
        size_ += align_ - (size_ % align_);
    }
    complete_ = true;
}

int RecordType::fieldIndex(std::string_view name) const {
    for (size_t i = 0; i < fields_.size(); ++i) {
        if (fields_[i].name == name) return (int)i;
    }
    return -1;
}

int RecordType::methodIndex(std::string_view name) const {
    for (size_t i = 0; i < methods_.size(); ++i) {
        if (methods_[i].name == name) return (int)i;
    }
    return -1;
}

const MethodInfo* RecordType::findConstructor(const std::vector<TypePtr>& argTypes) const {
    for (const auto& m : methods_) {
        if (!m.isConstructor) continue;
        if (!m.type || m.type->kind() != TypeKind::Function) continue;

        auto* ft = static_cast<FunctionType*>(m.type.get());
        if (ft->params().size() == argTypes.size()) {
            // Simple match for now: number of arguments
            return &m;
        }
    }
    return nullptr;
}

const MethodInfo* RecordType::findDestructor() const {
    for (const auto& m : methods_) {
        if (m.isDestructor) return &m;
    }
    return nullptr;
}

const MethodInfo* RecordType::findOperatorNew() const {
    for (const auto& m : methods_) {
        if (m.name == "operator new") return &m;
    }
    return nullptr;
}

const MethodInfo* RecordType::findOperatorDelete() const {
    for (const auto& m : methods_) {
        if (m.name == "operator delete") return &m;
    }
    return nullptr;
}

// ============================================================
// EnumType
// ============================================================
std::string EnumType::toString() const {
    return "enum " + name_;
}

// ============================================================
// QualType
// ============================================================
std::string QualType::toString() const {
    std::string prefix;
    if (quals_.isConst)    prefix += "const ";
    if (quals_.isVolatile) prefix += "volatile ";
    if (quals_.isRestrict) prefix += "restrict ";
    return prefix + (base_ ? base_->toString() : "<null>");
}

// ============================================================
// TypeContext constructor
// ============================================================
TypeContext::TypeContext() {
    auto addBuiltin = [&](TypeKind k) {
        auto t = std::make_shared<BuiltinType>(k);
        builtins_[(int)k] = t;
    };

    addBuiltin(TypeKind::Void);
    addBuiltin(TypeKind::Bool);
    addBuiltin(TypeKind::Char);
    addBuiltin(TypeKind::SChar);
    addBuiltin(TypeKind::UChar);
    addBuiltin(TypeKind::Short);
    addBuiltin(TypeKind::UShort);
    addBuiltin(TypeKind::Int);
    addBuiltin(TypeKind::UInt);
    addBuiltin(TypeKind::Long);
    addBuiltin(TypeKind::ULong);
    addBuiltin(TypeKind::LongLong);
    addBuiltin(TypeKind::ULongLong);
    addBuiltin(TypeKind::Float);
    addBuiltin(TypeKind::Double);
    addBuiltin(TypeKind::LongDouble);
    addBuiltin(TypeKind::NullptrT);
}

// ============================================================
// TypeContext factory methods
// ============================================================
TypePtr TypeContext::ptrTo(TypePtr base) {
    auto t = std::make_shared<PointerType>(std::move(base), TypeKind::Pointer);
    owned_.push_back(t);
    return t;
}

TypePtr TypeContext::refTo(TypePtr base) {
    auto t = std::make_shared<PointerType>(std::move(base), TypeKind::Reference);
    owned_.push_back(t);
    return t;
}

TypePtr TypeContext::arrayOf(TypePtr elem, i64 count) {
    auto t = std::make_shared<ArrayType>(std::move(elem), count);
    owned_.push_back(t);
    return t;
}

TypePtr TypeContext::qualOf(TypePtr base, QualFlags q) {
    auto t = std::make_shared<QualType>(std::move(base), q);
    owned_.push_back(t);
    return t;
}

TypePtr TypeContext::makeFn(TypePtr ret, std::vector<ParamInfo> params, bool variadic) {
    auto t = std::make_shared<FunctionType>(std::move(ret), std::move(params), variadic);
    owned_.push_back(t);
    return t;
}

Ref<RecordType> TypeContext::makeRecord(TypeKind k, std::string name) {
    auto t = std::make_shared<RecordType>(k, std::move(name));
    owned_.push_back(t);
    return t;
}

Ref<EnumType> TypeContext::makeEnum(std::string name) {
    auto t = std::make_shared<EnumType>(std::move(name));
    owned_.push_back(t);
    return t;
}

// ============================================================
// Helpers
// ============================================================
Type* stripQuals(Type* t) {
    while (t && t->kind() == TypeKind::Qualified) {
        auto* qt = static_cast<QualType*>(t);
        t = qt->base();
    }
    return t;
}

bool typeEqual(const Type* a, const Type* b) {
    if (a == b) return true;
    if (!a || !b) return false;

    // Strip qualified wrappers symmetrically
    const Type* sa = a;
    const Type* sb = b;
    while (sa && sa->kind() == TypeKind::Qualified)
        sa = static_cast<const QualType*>(sa)->base();
    while (sb && sb->kind() == TypeKind::Qualified)
        sb = static_cast<const QualType*>(sb)->base();

    if (!sa || !sb) return false;
    if (sa->kind() != sb->kind()) return false;

    switch (sa->kind()) {
    case TypeKind::Void:
    case TypeKind::Bool:
    case TypeKind::Char: case TypeKind::SChar: case TypeKind::UChar:
    case TypeKind::Short: case TypeKind::UShort:
    case TypeKind::Int: case TypeKind::UInt:
    case TypeKind::Long: case TypeKind::ULong:
    case TypeKind::LongLong: case TypeKind::ULongLong:
    case TypeKind::Float: case TypeKind::Double: case TypeKind::LongDouble:
    case TypeKind::NullptrT:
        // Same builtin kind => equal
        return true;

    case TypeKind::Pointer:
    case TypeKind::Reference:
    case TypeKind::RValueRef: {
        const auto* pa = static_cast<const PointerType*>(sa);
        const auto* pb = static_cast<const PointerType*>(sb);
        return typeEqual(pa->pointee(), pb->pointee());
    }

    case TypeKind::Array:
    case TypeKind::IncompleteArray: {
        const auto* aa = static_cast<const ArrayType*>(sa);
        const auto* ab = static_cast<const ArrayType*>(sb);
        if (aa->count() != ab->count()) return false;
        return typeEqual(aa->element(), ab->element());
    }

    case TypeKind::Function: {
        const auto* fa = static_cast<const FunctionType*>(sa);
        const auto* fb = static_cast<const FunctionType*>(sb);
        if (fa->isVariadic() != fb->isVariadic()) return false;
        if (!typeEqual(fa->returnType(), fb->returnType())) return false;
        const auto& pa = fa->params();
        const auto& pb = fb->params();
        if (pa.size() != pb.size()) return false;
        for (size_t i = 0; i < pa.size(); ++i) {
            if (!typeEqual(pa[i].type.get(), pb[i].type.get())) return false;
        }
        return true;
    }

    case TypeKind::Struct:
    case TypeKind::Union:
    case TypeKind::Class: {
        const auto* ra = static_cast<const RecordType*>(sa);
        const auto* rb = static_cast<const RecordType*>(sb);
        return ra == rb; // Nominal equality for records
    }

    case TypeKind::Enum: {
        const auto* ea = static_cast<const EnumType*>(sa);
        const auto* eb = static_cast<const EnumType*>(sb);
        return ea == eb; // Nominal equality for enums
    }

    default:
        return false;
    }
}

bool typeCompatible(const Type* a, const Type* b) {
    if (typeEqual(a, b)) return true;

    const Type* sa = stripQuals(const_cast<Type*>(a));
    const Type* sb = stripQuals(const_cast<Type*>(b));
    if (!sa || !sb) return false;

    // Allow array decay: T[] compatible with T*
    if (sa->kind() == TypeKind::Array || sa->kind() == TypeKind::IncompleteArray) {
        if (sb->kind() == TypeKind::Pointer) {
            const auto* arr = static_cast<const ArrayType*>(sa);
            const auto* ptr = static_cast<const PointerType*>(sb);
            return typeEqual(arr->element(), ptr->pointee());
        }
    }
    if (sb->kind() == TypeKind::Array || sb->kind() == TypeKind::IncompleteArray) {
        if (sa->kind() == TypeKind::Pointer) {
            const auto* arr = static_cast<const ArrayType*>(sb);
            const auto* ptr = static_cast<const PointerType*>(sa);
            return typeEqual(arr->element(), ptr->pointee());
        }
    }

    // Allow function-to-pointer decay
    if (sa->kind() == TypeKind::Function && sb->kind() == TypeKind::Pointer) {
        const auto* ptr = static_cast<const PointerType*>(sb);
        return typeEqual(sa, ptr->pointee());
    }
    if (sb->kind() == TypeKind::Function && sa->kind() == TypeKind::Pointer) {
        const auto* ptr = static_cast<const PointerType*>(sa);
        return typeEqual(sb, ptr->pointee());
    }

    return false;
}

} // namespace qc
