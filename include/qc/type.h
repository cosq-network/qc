#pragma once
#include "common.h"
#include <string>
#include <vector>

namespace qc {

class Type;
using TypePtr = Ref<Type>;

enum class TypeKind {
    Void,
    Bool,
    Char, SChar, UChar,
    Short, UShort,
    Int, UInt,
    Long, ULong,
    LongLong, ULongLong,
    Float, Double, LongDouble,
    // Derived
    Pointer, Reference, RValueRef,
    Array, IncompleteArray, VLA,
    Function,
    // Compound
    Struct, Union, Class, Enum,
    // C++
    NullptrT,
    // Qualified wrapper (const/volatile/restrict)
    Qualified,
    // Typedef alias
    Typedef,
    // Template
    TemplateParam,
};

struct QualFlags {
    bool isConst    = false;
    bool isVolatile = false;
    bool isRestrict = false;

    bool any() const { return isConst || isVolatile || isRestrict; }
};

class Type {
public:
    virtual ~Type() = default;
    virtual TypeKind kind() const = 0;
    virtual u32      size() const = 0;  // bytes
    virtual u32      align() const = 0; // bytes
    virtual std::string toString() const = 0;

    bool isVoid()     const { return kind() == TypeKind::Void; }
    bool isInteger()  const;
    bool isSigned()   const;
    bool isFloat()    const;
    bool isPointer()  const { return kind() == TypeKind::Pointer; }
    bool isArray()    const { return kind() == TypeKind::Array || kind() == TypeKind::IncompleteArray; }
    bool isFunction() const { return kind() == TypeKind::Function; }
    bool isStruct()   const { return kind() == TypeKind::Struct || kind() == TypeKind::Union || kind() == TypeKind::Class; }
    bool isArithmetic() const { return isInteger() || isFloat(); }
    bool isScalar()   const { return isArithmetic() || isPointer() || kind() == TypeKind::NullptrT || kind() == TypeKind::Enum; }

    const QualFlags& quals() const { return quals_; }
    void setQuals(QualFlags q) { quals_ = q; }

protected:
    QualFlags quals_;
};

// ---- Built-in types ----
class BuiltinType : public Type {
public:
    explicit BuiltinType(TypeKind k) : kind_(k) {}
    TypeKind kind()  const override { return kind_; }
    u32      size()  const override;
    u32      align() const override { return size(); }
    std::string toString() const override;

private:
    TypeKind kind_;
};

// ---- Pointer / Reference ----
class PointerType : public Type {
public:
    PointerType(TypePtr pointee, TypeKind k = TypeKind::Pointer)
        : pointee_(std::move(pointee)), kind_(k) {}
    TypeKind kind()  const override { return kind_; }
    u32      size()  const override { return 8; }
    u32      align() const override { return 8; }
    std::string toString() const override;
    Type* pointee() const { return pointee_.get(); }

private:
    TypePtr  pointee_;
    TypeKind kind_;
};

// ---- Array ----
class ArrayType : public Type {
public:
    ArrayType(TypePtr elem, i64 count)
        : elem_(std::move(elem)), count_(count) {}
    TypeKind kind()  const override { return count_ >= 0 ? TypeKind::Array : TypeKind::IncompleteArray; }
    u32      size()  const override { return count_ > 0 ? elem_->size() * (u32)count_ : 0; }
    u32      align() const override { return elem_->align(); }
    std::string toString() const override;
    Type* element() const { return elem_.get(); }
    i64   count()   const { return count_; }

private:
    TypePtr elem_;
    i64     count_;
};

// ---- Function ----
struct ParamInfo {
    std::string name;
    TypePtr     type;
};

class FunctionType : public Type {
public:
    FunctionType(TypePtr ret, std::vector<ParamInfo> params, bool variadic = false)
        : ret_(std::move(ret)), params_(std::move(params)), variadic_(variadic) {}
    TypeKind kind()  const override { return TypeKind::Function; }
    u32      size()  const override { return 0; }
    u32      align() const override { return 0; }
    std::string toString() const override;

    Type* returnType() const { return ret_.get(); }
    TypePtr returnTypePtr() const { return ret_; }
    const std::vector<ParamInfo>& params() const { return params_; }
    bool  isVariadic() const { return variadic_; }

private:
    TypePtr               ret_;
    std::vector<ParamInfo> params_;
    bool                  variadic_;
};

// ---- Struct / Union / Class ----
struct FieldInfo {
    std::string name;
    TypePtr     type;
    u32         offset = 0;  // byte offset within struct
    i32         bitWidth = -1; // -1 = not a bitfield
};

struct MethodInfo {
    std::string name;
    TypePtr     type; // FunctionType
    class RecordType* parent = nullptr;
    bool        isStatic = false;
    bool        isConstructor = false;
    bool        isDestructor = false;
};

class RecordType : public Type {
public:
    RecordType(TypeKind k, std::string name)
        : kind_(k), name_(std::move(name)) {}

    TypeKind kind()  const override { return kind_; }
    u32      size()  const override { return size_; }
    u32      align() const override { return align_; }
    std::string toString() const override;

    const std::string& name() const { return name_; }
    const std::vector<FieldInfo>& fields() const { return fields_; }
    const std::vector<MethodInfo>& methods() const { return methods_; }
    bool isComplete() const { return complete_; }

    void addField(FieldInfo f);
    void addMethod(MethodInfo m);
    void finalize();
    int  fieldIndex(std::string_view name) const;
    int  methodIndex(std::string_view name) const;

    const MethodInfo* findConstructor(const std::vector<TypePtr>& argTypes) const;
    const MethodInfo* findDestructor() const;

private:
    TypeKind            kind_;
    std::string         name_;
    std::vector<FieldInfo> fields_;
    std::vector<MethodInfo> methods_;
    u32                 size_   = 0;
    u32                 align_  = 1;
    bool                complete_ = false;
};

// ---- Enum ----
class EnumType : public Type {
public:
    struct Enumerator { std::string name; i64 value; };

    explicit EnumType(std::string name) : name_(std::move(name)) {}
    TypeKind kind()  const override { return TypeKind::Enum; }
    u32      size()  const override { return 4; }
    u32      align() const override { return 4; }
    std::string toString() const override;

    const std::string& name() const { return name_; }
    void addEnumerator(std::string name, i64 val) { enumerators_.push_back({std::move(name), val}); }
    const std::vector<Enumerator>& enumerators() const { return enumerators_; }

private:
    std::string              name_;
    std::vector<Enumerator>  enumerators_;
};

// ---- Qualified ----
class QualType : public Type {
public:
    QualType(TypePtr base, QualFlags q) : base_(std::move(base)) { quals_ = q; }
    TypeKind kind()  const override { return base_->kind(); }
    u32      size()  const override { return base_->size(); }
    u32      align() const override { return base_->align(); }
    std::string toString() const override;
    Type* base() const { return base_.get(); }

private:
    TypePtr base_;
};

// ---- Type factory / cache ----
class TypeContext {
public:
    TypeContext();

    TypePtr voidTy()      { return builtins_[(int)TypeKind::Void]; }
    TypePtr boolTy()      { return builtins_[(int)TypeKind::Bool]; }
    TypePtr charTy()      { return builtins_[(int)TypeKind::Char]; }
    TypePtr scharTy()     { return builtins_[(int)TypeKind::SChar]; }
    TypePtr ucharTy()     { return builtins_[(int)TypeKind::UChar]; }
    TypePtr shortTy()     { return builtins_[(int)TypeKind::Short]; }
    TypePtr ushortTy()    { return builtins_[(int)TypeKind::UShort]; }
    TypePtr intTy()       { return builtins_[(int)TypeKind::Int]; }
    TypePtr uintTy()      { return builtins_[(int)TypeKind::UInt]; }
    TypePtr longTy()      { return builtins_[(int)TypeKind::Long]; }
    TypePtr ulongTy()     { return builtins_[(int)TypeKind::ULong]; }
    TypePtr longlongTy()  { return builtins_[(int)TypeKind::LongLong]; }
    TypePtr ulonglongTy() { return builtins_[(int)TypeKind::ULongLong]; }
    TypePtr floatTy()     { return builtins_[(int)TypeKind::Float]; }
    TypePtr doubleTy()    { return builtins_[(int)TypeKind::Double]; }
    TypePtr nullptrTTy()  { return builtins_[(int)TypeKind::NullptrT]; }

    TypePtr ptrTo(TypePtr base);
    TypePtr refTo(TypePtr base);
    TypePtr arrayOf(TypePtr elem, i64 count);
    TypePtr qualOf(TypePtr base, QualFlags q);
    TypePtr makeFn(TypePtr ret, std::vector<ParamInfo> params, bool variadic = false);

    Ref<RecordType> makeRecord(TypeKind k, std::string name);
    Ref<EnumType>   makeEnum(std::string name);

private:
    std::unordered_map<int, TypePtr>  builtins_;
    std::vector<TypePtr>              owned_;
};

// Helpers
Type* stripQuals(Type* t);
bool  typeEqual(const Type* a, const Type* b);
bool  typeCompatible(const Type* a, const Type* b);

} // namespace qc
