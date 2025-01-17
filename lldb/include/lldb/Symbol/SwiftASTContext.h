//===-- SwiftASTContext.h ---------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_SwiftASTContext_h_
#define liblldb_SwiftASTContext_h_

#include "Plugins/ExpressionParser/Swift/SwiftPersistentExpressionState.h"
#include "lldb/Core/ClangForward.h"
#include "lldb/Core/SwiftForward.h"
#include "lldb/Core/ThreadSafeDenseMap.h"
#include "lldb/Core/ThreadSafeDenseSet.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private.h"

#include "lldb/Utility/Either.h"
#include "lldb/Utility/Status.h"

#include "llvm/ADT/Optional.h"
#include "llvm/Support/Threading.h"
#include "llvm/Target/TargetOptions.h"

#include <map>
#include <set>

namespace swift {
enum class IRGenDebugInfoLevel : unsigned;
class CanType;
class DependencyTracker;
class DWARFImporterDelegate;
class IRGenOptions;
class NominalTypeDecl;
class SearchPathOptions;
class SILModule;
class VarDecl;
class ModuleDecl;
class SourceFile;
struct PrintOptions;
class MemoryBufferSerializedModuleLoader;
namespace Demangle {
class Demangler;
class Node;
using NodePointer = Node *;
} // namespace Demangle
namespace irgen {
class FixedTypeInfo;
class TypeInfo;
} // namespace irgen
namespace serialization {
struct ValidationInfo;
class ExtendedValidationInfo;
} // namespace serialization
namespace Lowering {
class TypeConverter;
}
} // namespace swift

class DWARFASTParser;
class SwiftEnumDescriptor;

namespace lldb_private {

struct SourceModule;
class SwiftASTContext;
CompilerType ToCompilerType(swift::Type qual_type);

/// The implementation of lldb::Type's m_payload field for TypeSystemSwift.
class TypePayloadSwift {
  /// Layout: bit 1 ... IsFixedValueBuffer.
  Type::Payload m_payload = 0;

  static constexpr unsigned FixedValueBufferBit = 1;
public:
  TypePayloadSwift() = default;
  explicit TypePayloadSwift(bool is_fixed_value_buffer);
  explicit TypePayloadSwift(Type::Payload opaque_payload)
      : m_payload(opaque_payload) {}
  operator Type::Payload() { return m_payload; }

  /// \return whether this is a Swift fixed-size buffer. Resilient variables in
  /// fixed-size buffers may be indirect depending on the runtime size of the
  /// type. This is more a property of the value than of its type.
  bool IsFixedValueBuffer() { return Flags(m_payload).Test(FixedValueBufferBit); }
  void SetIsFixedValueBuffer(bool is_fixed_value_buffer) {
    m_payload = is_fixed_value_buffer
                    ? Flags(m_payload).Set(FixedValueBufferBit)
                    : Flags(m_payload).Clear(FixedValueBufferBit);
  }
};
  
/// Abstract base class for all Swift TypeSystems.
///
/// Swift CompilerTypes are either a mangled name or a Swift AST
/// type. If the typesystem is a TypeSystemSwiftTypeRef, they are
/// mangled names.
///
/// \verbatim
///                      TypeSystem (abstract)
///                            │
///                            ↓
///                      TypeSystemSwift (abstract)
///                        │         │
///                        ↓         ↓
///    TypeSystemSwiftTypeRef ⟷ SwiftASTContext (deprecated)
///                                  │
///                                  ↓
///                               SwiftASTContextForExpressions
///
/// \endverbatim
class TypeSystemSwift : public TypeSystem {
  /// LLVM RTTI support.
  static char ID;

public:
  /// LLVM RTTI support.
  /// \{
  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }
  /// \}

  TypeSystemSwift();

  virtual lldb::TypeSP GetCachedType(ConstString mangled) = 0;
  virtual void SetCachedType(ConstString mangled,
                             const lldb::TypeSP &type_sp) = 0;
  virtual bool IsImportedType(CompilerType type,
                              CompilerType *original_type) = 0;
  virtual bool IsErrorType(CompilerType compiler_type) = 0;
  virtual CompilerType GetErrorType() = 0;
  virtual CompilerType GetReferentType(CompilerType compiler_type) = 0;
  static CompilerType GetInstanceType(CompilerType ct);
  virtual CompilerType GetInstanceType(void *type) = 0;
  enum class TypeAllocationStrategy { eInline, ePointer, eDynamic, eUnknown };
  virtual TypeAllocationStrategy GetAllocationStrategy(CompilerType type) = 0;
  struct TupleElement {
    ConstString element_name;
    CompilerType element_type;
  };
  virtual CompilerType
  CreateTupleType(const std::vector<TupleElement> &elements) = 0;
  virtual void DumpTypeDescription(void *type, bool print_help_if_available,
                                   bool print_extensions_if_available) = 0;
  virtual void DumpTypeDescription(void *type, Stream *s,
                                   bool print_help_if_available,
                                   bool print_extensions_if_available) = 0;

  /// Unavailable hardcoded functions that don't make sense for Swift.
  /// \{
  ConstString DeclContextGetName(void *opaque_decl_ctx) override { return {}; }
  ConstString DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) override {
    return {};
  }
  bool
  DeclContextIsClassMethod(void *opaque_decl_ctx,
                           lldb::LanguageType *language_ptr,
                           bool *is_instance_method_ptr,
                           ConstString *language_object_name_ptr) override {
    return false;
  }
  bool IsRuntimeGeneratedType(void *type) override { return false; }
  bool IsCharType(void *type) override { return false; }
  bool IsCompleteType(void *type) override { return true; }
  bool IsConst(void *type) override { return false; }
  bool IsCStringType(void *type, uint32_t &length) override { return false; }
  bool IsVectorType(void *type, CompilerType *element_type,
                    uint64_t *size) override {
    return false;
  }
  uint32_t IsHomogeneousAggregate(void *type,
                                  CompilerType *base_type_ptr) override {
    return 0;
  }
  bool IsBlockPointerType(void *type,
                          CompilerType *function_pointer_type_ptr) override {
    return false;
  }
  bool IsPolymorphicClass(void *type) override { return false; }
  bool IsBeingDefined(void *type) override { return false; }
  unsigned GetTypeQualifiers(void *type) override { return 0; }
  CompilerType GetTypeForDecl(void *opaque_decl) override {
    llvm_unreachable("GetTypeForDecl not implemented");
  }
  CompilerType GetBasicTypeFromAST(lldb::BasicType basic_type) override {
    return {};
  }
  const llvm::fltSemantics &GetFloatTypeSemantics(size_t byte_size) override {
    // See: https://reviews.llvm.org/D67239. At this time of writing this API
    // is only used by DumpDataExtractor for the C type system.
    llvm_unreachable("GetFloatTypeSemantics not implemented.");
  }
  lldb::BasicType GetBasicTypeEnumeration(void *type) override {
    return lldb::eBasicTypeInvalid;
  }
  uint32_t GetNumVirtualBaseClasses(void *opaque_type) override { return 0; }
  CompilerType GetVirtualBaseClassAtIndex(void *opaque_type, size_t idx,
                                          uint32_t *bit_offset_ptr) override {
    return {};
  }
  /// \}
protected:
  /// Used in the logs.
  std::string m_description;
};

/// A Swift TypeSystem that does not own a swift::ASTContext.
class TypeSystemSwiftTypeRef : public TypeSystemSwift {
  /// LLVM RTTI support.
  static char ID;

public:
  /// LLVM RTTI support.
  /// \{
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || TypeSystemSwift::isA(ClassID);
  }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }
  /// \}

  TypeSystemSwiftTypeRef(SwiftASTContext *swift_ast_context);

  Module *GetModule() const;
  swift::CanType GetCanonicalSwiftType(CompilerType compiler_type);
  swift::Type GetSwiftType(CompilerType compiler_type);
  CompilerType ReconstructType(CompilerType type);
  CompilerType GetTypeFromMangledTypename(ConstString mangled_typename);

  // PluginInterface functions
  ConstString GetPluginName() override;
  uint32_t GetPluginVersion() override;

  bool SupportsLanguage(lldb::LanguageType language) override;
  Status IsCompatible() override;

  void DiagnoseWarnings(Process &process, Module &module) const override;
  DWARFASTParser *GetDWARFParser() override;
  // CompilerDecl functions
  ConstString DeclGetName(void *opaque_decl) override {
    return ConstString("");
  }

  // CompilerDeclContext functions
  std::vector<CompilerDecl>
  DeclContextFindDeclByName(void *opaque_decl_ctx, ConstString name,
                            const bool ignore_imported_decls) override {
    return {};
  }

  bool DeclContextIsContainedInLookup(void *opaque_decl_ctx,
                                      void *other_opaque_decl_ctx) override {
    if (opaque_decl_ctx == other_opaque_decl_ctx)
      return true;
    return false;
  }

  // Tests
#ifndef NDEBUG
  bool Verify(lldb::opaque_compiler_type_t type) override;
#endif
  bool IsArrayType(void *type, CompilerType *element_type, uint64_t *size,
                   bool *is_incomplete) override;
  bool IsAggregateType(void *type) override;
  bool IsDefined(void *type) override;
  bool IsFloatingPointType(void *type, uint32_t &count,
                           bool &is_complex) override;
  bool IsFunctionType(void *type, bool *is_variadic_ptr) override;
  size_t GetNumberOfFunctionArguments(void *type) override;
  CompilerType GetFunctionArgumentAtIndex(void *type,
                                          const size_t index) override;
  bool IsFunctionPointerType(void *type) override;
  bool IsIntegerType(void *type, bool &is_signed) override;
  bool IsPossibleDynamicType(void *type,
                             CompilerType *target_type, // Can pass NULL
                             bool check_cplusplus, bool check_objc) override;
  bool IsPointerType(void *type, CompilerType *pointee_type) override;
  bool IsScalarType(void *type) override;
  bool IsVoidType(void *type) override;
  bool CanPassInRegisters(const CompilerType &type) override;
  // Type Completion
  bool GetCompleteType(void *type) override;
  // AST related queries
  uint32_t GetPointerByteSize() override;
  // Accessors
  ConstString GetTypeName(void *type) override;
  ConstString GetDisplayTypeName(void *type, const SymbolContext *sc) override;
  ConstString GetMangledTypeName(void *type) override;
  uint32_t GetTypeInfo(void *type,
                       CompilerType *pointee_or_element_clang_type) override;
  lldb::LanguageType GetMinimumLanguage(void *type) override;
  lldb::TypeClass GetTypeClass(void *type) override;

  // Creating related types
  CompilerType GetArrayElementType(void *type, uint64_t *stride) override;
  CompilerType GetCanonicalType(void *type) override;
  int GetFunctionArgumentCount(void *type) override;
  CompilerType GetFunctionArgumentTypeAtIndex(void *type, size_t idx) override;
  CompilerType GetFunctionReturnType(void *type) override;
  size_t GetNumMemberFunctions(void *type) override;
  TypeMemberFunctionImpl GetMemberFunctionAtIndex(void *type,
                                                  size_t idx) override;
  CompilerType GetPointeeType(void *type) override;
  CompilerType GetPointerType(void *type) override;

  // Exploring the type
  llvm::Optional<uint64_t>
  GetBitSize(lldb::opaque_compiler_type_t type,
             ExecutionContextScope *exe_scope) override;
  llvm::Optional<uint64_t>
  GetByteStride(lldb::opaque_compiler_type_t type,
                ExecutionContextScope *exe_scope) override;
  lldb::Encoding GetEncoding(void *type, uint64_t &count) override;
  lldb::Format GetFormat(void *type) override;
  uint32_t GetNumChildren(void *type, bool omit_empty_base_classes,
                          const ExecutionContext *exe_ctx) override;
  uint32_t GetNumFields(void *type) override;
  CompilerType GetFieldAtIndex(void *type, size_t idx, std::string &name,
                               uint64_t *bit_offset_ptr,
                               uint32_t *bitfield_bit_size_ptr,
                               bool *is_bitfield_ptr) override;
  CompilerType GetChildCompilerTypeAtIndex(
      void *type, ExecutionContext *exe_ctx, size_t idx,
      bool transparent_pointers, bool omit_empty_base_classes,
      bool ignore_array_bounds, std::string &child_name,
      uint32_t &child_byte_size, int32_t &child_byte_offset,
      uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
      bool &child_is_base_class, bool &child_is_deref_of_parent,
      ValueObject *valobj, uint64_t &language_flags) override;
  uint32_t GetIndexOfChildWithName(void *type, const char *name,
                                   bool omit_empty_base_classes) override;
  size_t
  GetIndexOfChildMemberWithName(void *type, const char *name,
                                bool omit_empty_base_classes,
                                std::vector<uint32_t> &child_indexes) override;
  size_t GetNumTemplateArguments(void *type) override;
  CompilerType GetTypeForFormatters(void *type) override;
  LazyBool ShouldPrintAsOneLiner(void *type, ValueObject *valobj) override;
  bool IsMeaninglessWithoutDynamicResolution(void *type) override;

  // Dumping types
#ifndef NDEBUG
  /// Convenience LLVM-style dump method for use in the debugger only.
  LLVM_DUMP_METHOD virtual void
  dump(lldb::opaque_compiler_type_t type) const override;
#endif

  void DumpValue(void *type, ExecutionContext *exe_ctx, Stream *s,
                 lldb::Format format, const DataExtractor &data,
                 lldb::offset_t data_offset, size_t data_byte_size,
                 uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                 bool show_types, bool show_summary, bool verbose,
                 uint32_t depth) override;

  bool DumpTypeValue(void *type, Stream *s, lldb::Format format,
                     const DataExtractor &data, lldb::offset_t data_offset,
                     size_t data_byte_size, uint32_t bitfield_bit_size,
                     uint32_t bitfield_bit_offset,
                     ExecutionContextScope *exe_scope,
                     bool is_base_class) override;

  void DumpTypeDescription(void *type) override;
  void DumpTypeDescription(void *type, Stream *s) override;
  void DumpSummary(void *type, ExecutionContext *exe_ctx, Stream *s,
                   const DataExtractor &data, lldb::offset_t data_offset,
                   size_t data_byte_size) override;
  bool IsPointerOrReferenceType(void *type,
                                CompilerType *pointee_type) override;
  llvm::Optional<size_t>
  GetTypeBitAlign(void *type, ExecutionContextScope *exe_scope) override;
  CompilerType GetBuiltinTypeForEncodingAndBitSize(lldb::Encoding encoding,
                                                   size_t bit_size) override {
    return CompilerType();
  }
  bool IsTypedefType(void *type) override;
  CompilerType GetTypedefedType(void *type) override;
  CompilerType GetFullyUnqualifiedType(void *type) override;
  CompilerType GetNonReferenceType(void *type) override;
  CompilerType GetLValueReferenceType(void *type) override;
  CompilerType GetRValueReferenceType(void *opaque_type) override;
  uint32_t GetNumDirectBaseClasses(void *opaque_type) override;
  CompilerType GetDirectBaseClassAtIndex(void *opaque_type, size_t idx,
                                         uint32_t *bit_offset_ptr) override;
  bool IsReferenceType(void *type, CompilerType *pointee_type,
                       bool *is_rvalue) override;
  bool
  ShouldTreatScalarValueAsAddress(lldb::opaque_compiler_type_t type) override;

  // Swift-specific methods.
  lldb::TypeSP GetCachedType(ConstString mangled) override;
  void SetCachedType(ConstString mangled, const lldb::TypeSP &type_sp) override;
  bool IsImportedType(CompilerType type, CompilerType *original_type) override;
  bool IsErrorType(CompilerType compiler_type) override;
  CompilerType GetErrorType() override;
  CompilerType GetReferentType(CompilerType compiler_type) override;
  CompilerType GetInstanceType(void *type) override;
  TypeAllocationStrategy GetAllocationStrategy(CompilerType type) override;
  CompilerType
  CreateTupleType(const std::vector<TupleElement> &elements) override;
  void DumpTypeDescription(void *type, bool print_help_if_available,
                           bool print_extensions_if_available) override;
  void DumpTypeDescription(void *type, Stream *s, bool print_help_if_available,
                           bool print_extensions_if_available) override;

private:
  /// Helper that creates an AST type from \p type.
  void *ReconstructType(void *type);
  /// Cast \p opaque_type as a mangled name.
  const char *AsMangledName(void *opaque_type);

  /// Wrap \p node as \p Global(TypeMangling(node)), remangle the type
  /// and create a CompilerType from it.
  CompilerType RemangleAsType(swift::Demangle::Demangler &Dem,
                              swift::Demangle::NodePointer node);

  /// The sibling SwiftASTContext.
  SwiftASTContext *m_swift_ast_context = nullptr;
};

/// This "middle" class between TypeSystemSwiftTypeRef and
/// SwiftASTContextForExpressions will eventually go away, as more and
/// more functionality becomes available in TypeSystemSwiftTypeRef.
class SwiftASTContext : public TypeSystemSwift {
  /// LLVM RTTI support.
  static char ID;

public:
  typedef lldb_utility::Either<CompilerType, swift::Decl *> TypeOrDecl;

  /// LLVM RTTI support.
  /// \{
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || TypeSystemSwift::isA(ClassID);
  }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }
  /// \}

private:
  struct EitherComparator {
    bool operator()(const TypeOrDecl &r1, const TypeOrDecl &r2) const {
      auto r1_as1 = r1.GetAs<CompilerType>();
      auto r1_as2 = r1.GetAs<swift::Decl *>();

      auto r2_as1 = r2.GetAs<CompilerType>();
      auto r2_as2 = r2.GetAs<swift::Decl *>();

      if (r1_as1.hasValue() && r2_as1.hasValue())
        return r1_as1.getValue() < r2_as1.getValue();

      if (r1_as2.hasValue() && r2_as2.hasValue())
        return r1_as2.getValue() < r2_as2.getValue();

      if (r1_as1.hasValue() && r2_as2.hasValue())
        return (void *)r1_as1->GetOpaqueQualType() < (void *)r2_as2.getValue();

      if (r1_as2.hasValue() && r2_as1.hasValue())
        return (void *)r1_as2.getValue() < (void *)r2_as1->GetOpaqueQualType();

      return false;
    }
  };

public:
  typedef std::set<TypeOrDecl, EitherComparator> TypesOrDecls;

  class LanguageFlags {
  public:
    enum : uint64_t {
      eIsIndirectEnumCase = 0x1ULL,
      eIgnoreInstancePointerness = 0x2ULL
    };

  private:
    LanguageFlags() = delete;
  };

  /// Provide the global LLVMContext.
  static llvm::LLVMContext &GetGlobalLLVMContext();

  // Constructors and destructors
  SwiftASTContext(std::string description, llvm::Triple triple,
                  Target *target = nullptr);

  SwiftASTContext(const SwiftASTContext &rhs) = delete;

  ~SwiftASTContext();

  const std::string &GetDescription() const;

  // PluginInterface functions
  ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

  static ConstString GetPluginNameStatic();

  /// Create a SwiftASTContext from a Module.  This context is used
  /// for frame variable and uses ClangImporter options specific to
  /// this lldb::Module.  The optional target is necessary when
  /// creating a module-specific scratch context.  If \p fallback is
  /// true, then a SwiftASTContextForExpressions is created.
  static lldb::TypeSystemSP CreateInstance(lldb::LanguageType language,
                                           Module &module,
                                           Target *target = nullptr,
                                           bool fallback = false);
  /// Create a SwiftASTContext from a Target.  This context is global
  /// and used for the expression evaluator.
  static lldb::TypeSystemSP CreateInstance(lldb::LanguageType language,
                                           Target &target,
                                           const char *extra_options);

  static void EnumerateSupportedLanguages(
      std::set<lldb::LanguageType> &languages_for_types,
      std::set<lldb::LanguageType> &languages_for_expressions);

  static void Initialize();

  static void Terminate();

  bool SupportsLanguage(lldb::LanguageType language) override;

  Status IsCompatible() override;

  swift::SourceManager &GetSourceManager();

  swift::LangOptions &GetLanguageOptions();

  swift::TypeCheckerOptions &GetTypeCheckerOptions();

  swift::DiagnosticEngine &GetDiagnosticEngine();

  swift::SearchPathOptions &GetSearchPathOptions();

  void InitializeSearchPathOptions(
      llvm::ArrayRef<std::string> module_search_paths,
      llvm::ArrayRef<std::pair<std::string, bool>> framework_search_paths);

  swift::ClangImporterOptions &GetClangImporterOptions();

  swift::CompilerInvocation &GetCompilerInvocation();

  swift::SILOptions &GetSILOptions();

  swift::ASTContext *GetASTContext();

  swift::IRGenDebugInfoLevel GetGenerateDebugInfo();

  static swift::PrintOptions
  GetUserVisibleTypePrintingOptions(bool print_help_if_available);

  void SetGenerateDebugInfo(swift::IRGenDebugInfoLevel b);

  bool AddModuleSearchPath(llvm::StringRef path);

  bool AddClangArgument(std::string arg, bool unique = true);

  bool AddClangArgumentPair(llvm::StringRef arg1, llvm::StringRef arg2);

  /// Add a list of Clang arguments to the ClangImporter options and
  /// apply the working directory to any relative paths.
  void AddExtraClangArgs(std::vector<std::string> ExtraArgs);

  /// Add the target's swift-extra-clang-flags to the ClangImporter options.
  void AddUserClangArgs(TargetProperties &props);

  llvm::StringRef GetPlatformSDKPath() const { return m_platform_sdk_path; }

  void SetPlatformSDKPath(std::string &&sdk_path) {
    m_platform_sdk_path = sdk_path;
  }

  void SetPlatformSDKPath(llvm::StringRef path) { m_platform_sdk_path = path; }

  const swift::SearchPathOptions *GetSearchPathOptions() const;

  /// \return the ExtraArgs of the ClangImporterOptions.
  const std::vector<std::string> &GetClangArguments();

  swift::ModuleDecl *CreateModule(const SourceModule &module, Status &error);

  // This function should only be called when all search paths
  // for all items in a swift::ASTContext have been setup to
  // allow for imports to happen correctly. Use with caution,
  // or use the GetModule() call that takes a FileSpec.
  swift::ModuleDecl *GetModule(const SourceModule &module, Status &error);

  swift::ModuleDecl *GetModule(const FileSpec &module_spec, Status &error);

  void CacheModule(swift::ModuleDecl *module);

  Module *GetModule() const { return m_module; }

  // Call this after the search paths are set up, it will find the module given
  // by module, load the module into the AST context, and also load any
  // "LinkLibraries" that the module requires.

  swift::ModuleDecl *FindAndLoadModule(const SourceModule &module,
                                       Process &process, Status &error);

  swift::ModuleDecl *FindAndLoadModule(const FileSpec &module_spec,
                                       Process &process, Status &error);

  void LoadModule(swift::ModuleDecl *swift_module, Process &process,
                  Status &error);

  /// Collect Swift modules in the .swift_ast section of \p module.
  void RegisterSectionModules(Module &module,
                              std::vector<std::string> &module_names);

  void ValidateSectionModules(Module &module, // this is used to print errors
                              const std::vector<std::string> &module_names);

  // Swift modules that are backed by dylibs (libFoo.dylib) rather than
  // frameworks don't actually record the library dependencies in the module.
  // This will hand load any libraries that are on the IRGen LinkLibraries list
  // using the compiler's search paths.
  // It doesn't do frameworks since frameworks don't need it and this is kind of
  // a hack anyway.

  void LoadExtraDylibs(Process &process, Status &error);

  swift::Identifier GetIdentifier(const llvm::StringRef &name);

  CompilerType FindType(const char *name, swift::ModuleDecl *swift_module);

  llvm::Optional<SwiftASTContext::TypeOrDecl>
  FindTypeOrDecl(const char *name, swift::ModuleDecl *swift_module);

  size_t FindTypes(const char *name, swift::ModuleDecl *swift_module,
                   std::set<CompilerType> &results, bool append = true);

  size_t FindTypesOrDecls(const char *name, swift::ModuleDecl *swift_module,
                          TypesOrDecls &results, bool append = true);

  size_t FindContainedTypeOrDecl(llvm::StringRef name,
                                 TypeOrDecl container_type_or_decl,
                                 TypesOrDecls &results, bool append = true);

  size_t FindType(const char *name, std::set<CompilerType> &results,
                  bool append = true);

  /// Reconstruct a Swift AST type from a mangled name by looking its
  /// components up in Swift modules.
  swift::TypeBase *ReconstructType(ConstString mangled_typename);
  swift::TypeBase *ReconstructType(ConstString mangled_typename, Status &error);
  CompilerType GetTypeFromMangledTypename(ConstString mangled_typename);

  // Retrieve the Swift.AnyObject type.
  CompilerType GetAnyObjectType();

  // Get a function type that returns nothing and take no parameters
  CompilerType GetVoidFunctionType();

  /// Import and Swiftify a Clang type.
  /// \return Returns an invalid type if unsuccessful.
  CompilerType ImportClangType(CompilerType clang_type);

  static SwiftASTContext *GetSwiftASTContext(swift::ASTContext *ast);

  swift::irgen::IRGenerator &GetIRGenerator(swift::IRGenOptions &opts,
                                            swift::SILModule &module);

  swift::irgen::IRGenModule &GetIRGenModule();

  lldb::TargetWP GetTarget() const { return m_target_wp; }

  llvm::Triple GetTriple() const;

  bool SetTriple(const llvm::Triple triple,
                 lldb_private::Module *module = nullptr);

  CompilerType GetCompilerType(swift::TypeBase *swift_type);
  CompilerType GetCompilerType(ConstString mangled_name);
  swift::Type GetSwiftType(CompilerType compiler_type);
  swift::Type GetSwiftType(void *opaque_type);
  swift::CanType GetCanonicalSwiftType(void *opaque_type);

  // Imports the type from the passed in type into this SwiftASTContext. The
  // type must be a Swift type. If the type can be imported, returns the
  // CompilerType for the imported type.
  // If it cannot be, returns an invalid CompilerType, and sets the error to
  // indicate what went wrong.
  CompilerType ImportType(CompilerType &type, Status &error);

  swift::ClangImporter *GetClangImporter();
  swift::DWARFImporterDelegate *GetDWARFImporterDelegate();

  CompilerType CreateTupleType(const std::vector<TupleElement> &elements);

  CompilerType GetErrorType();

  bool HasErrors();

  // NEVER call this without checking HasFatalErrors() first.
  // This clears the fatal-error state which is terrible.
  // We will assert if you clear an actual fatal error.
  void ClearDiagnostics();

  bool SetColorizeDiagnostics(bool b);
  void AddErrorStatusAsGenericDiagnostic(Status error);

  void PrintDiagnostics(DiagnosticManager &diagnostic_manager,
                        uint32_t bufferID = UINT32_MAX, uint32_t first_line = 0,
                        uint32_t last_line = UINT32_MAX);

  ConstString GetMangledTypeName(swift::TypeBase *);

  swift::IRGenOptions &GetIRGenOptions();

  void ModulesDidLoad(ModuleList &module_list);

  void ClearModuleDependentCaches();

  void LogConfiguration();

  bool HasTarget() const;

  bool CheckProcessChanged();

  // FIXME: this should be removed once we figure out who should really own the
  // DebuggerClient's that we are sticking into the Swift Modules.
  void AddDebuggerClient(swift::DebuggerClient *debugger_client);

  typedef llvm::StringMap<swift::ModuleDecl *> SwiftModuleMap;

  const SwiftModuleMap &GetModuleCache() { return m_swift_module_cache; }

  static bool HasFatalErrors(swift::ASTContext *ast_context);

  bool HasFatalErrors() const {
    return m_fatal_errors.Fail() || HasFatalErrors(m_ast_context_ap.get());
  }

  Status GetFatalErrors();
  void DiagnoseWarnings(Process &process, Module &module) const override;

  const swift::irgen::TypeInfo *GetSwiftTypeInfo(void *type);

  const swift::irgen::FixedTypeInfo *GetSwiftFixedTypeInfo(void *type);

  bool IsFixedSize(CompilerType compiler_type);

  DWARFASTParser *GetDWARFParser() override;

  // CompilerDecl functions
  ConstString DeclGetName(void *opaque_decl) override {
    return ConstString("");
  }

  // CompilerDeclContext functions

  std::vector<CompilerDecl>
  DeclContextFindDeclByName(void *opaque_decl_ctx, ConstString name,
                            const bool ignore_imported_decls) override {
    return {};
  }

  bool DeclContextIsContainedInLookup(void *opaque_decl_ctx,
                                      void *other_opaque_decl_ctx) override {
    if (opaque_decl_ctx == other_opaque_decl_ctx)
      return true;
    return false;
  }

  // Tests

#ifndef NDEBUG
  bool Verify(lldb::opaque_compiler_type_t type) override;
#endif

  bool IsArrayType(void *type, CompilerType *element_type, uint64_t *size,
                   bool *is_incomplete) override;

  bool IsAggregateType(void *type) override;

  bool IsDefined(void *type) override;

  bool IsFloatingPointType(void *type, uint32_t &count,
                           bool &is_complex) override;

  bool IsFunctionType(void *type, bool *is_variadic_ptr) override;

  size_t GetNumberOfFunctionArguments(void *type) override;

  CompilerType GetFunctionArgumentAtIndex(void *type,
                                          const size_t index) override;

  bool IsFunctionPointerType(void *type) override;

  bool IsIntegerType(void *type, bool &is_signed) override;

  bool IsPossibleDynamicType(void *type,
                             CompilerType *target_type, // Can pass NULL
                             bool check_cplusplus, bool check_objc) override;

  bool IsPointerType(void *type, CompilerType *pointee_type) override;

  bool IsScalarType(void *type) override;

  bool IsVoidType(void *type) override;

  bool CanPassInRegisters(const CompilerType &type) override;

  static bool IsGenericType(const CompilerType &compiler_type);

  bool IsErrorType(CompilerType compiler_type) override;

  static bool IsFullyRealized(const CompilerType &compiler_type);

  struct ProtocolInfo {
    uint32_t m_num_protocols;
    uint32_t m_num_payload_words;
    uint32_t m_num_storage_words;
    bool m_is_class_only;
    bool m_is_objc;
    bool m_is_anyobject;
    bool m_is_errortype;

    /// The superclass bound, which can only be non-null when this is
    /// a class-bound existential.
    CompilerType m_superclass;

    /// The member index for the error value within an error
    /// existential.
    static constexpr uint32_t error_instance_index = 0;

    /// Retrieve the index at which the instance type occurs.
    uint32_t GetInstanceTypeIndex() const { return m_num_payload_words; }
  };

  static bool GetProtocolTypeInfo(const CompilerType &type,
                                  ProtocolInfo &protocol_info);

  TypeAllocationStrategy GetAllocationStrategy(CompilerType type) override;

  enum class NonTriviallyManagedReferenceStrategy {
    eWeak,
    eUnowned,
    eUnmanaged
  };

  static bool IsNonTriviallyManagedReferenceType(
      const CompilerType &type, NonTriviallyManagedReferenceStrategy &strategy,
      CompilerType *underlying_type = nullptr);

  // Type Completion

  bool GetCompleteType(void *type) override;

  // AST related queries

  uint32_t GetPointerByteSize() override;

  // Accessors

  ConstString GetTypeName(void *type) override;

  ConstString GetDisplayTypeName(void *type, const SymbolContext *sc) override;

  ConstString GetMangledTypeName(void *type) override;

  uint32_t GetTypeInfo(void *type,
                       CompilerType *pointee_or_element_clang_type) override;

  lldb::LanguageType GetMinimumLanguage(void *type) override;

  lldb::TypeClass GetTypeClass(void *type) override;

  // Creating related types

  CompilerType GetArrayElementType(void *type, uint64_t *stride) override;

  CompilerType GetCanonicalType(void *type) override;

  CompilerType GetInstanceType(void *type) override;

  // Returns -1 if this isn't a function of if the function doesn't have a
  // prototype. Returns a value >override if there is a prototype.
  int GetFunctionArgumentCount(void *type) override;

  CompilerType GetFunctionArgumentTypeAtIndex(void *type, size_t idx) override;

  CompilerType GetFunctionReturnType(void *type) override;

  size_t GetNumMemberFunctions(void *type) override;

  TypeMemberFunctionImpl GetMemberFunctionAtIndex(void *type,
                                                  size_t idx) override;

  CompilerType GetPointeeType(void *type) override;

  CompilerType GetPointerType(void *type) override;

  // Exploring the type

  llvm::Optional<uint64_t>
  GetBitSize(lldb::opaque_compiler_type_t type,
             ExecutionContextScope *exe_scope) override;

  llvm::Optional<uint64_t>
  GetByteStride(lldb::opaque_compiler_type_t type,
                ExecutionContextScope *exe_scope) override;

  lldb::Encoding GetEncoding(void *type, uint64_t &count) override;

  lldb::Format GetFormat(void *type) override;

  uint32_t GetNumChildren(void *type, bool omit_empty_base_classes,
                          const ExecutionContext *exe_ctx) override;

  uint32_t GetNumFields(void *type) override;

  CompilerType GetFieldAtIndex(void *type, size_t idx, std::string &name,
                               uint64_t *bit_offset_ptr,
                               uint32_t *bitfield_bit_size_ptr,
                               bool *is_bitfield_ptr) override;

  CompilerType GetChildCompilerTypeAtIndex(
      void *type, ExecutionContext *exe_ctx, size_t idx,
      bool transparent_pointers, bool omit_empty_base_classes,
      bool ignore_array_bounds, std::string &child_name,
      uint32_t &child_byte_size, int32_t &child_byte_offset,
      uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
      bool &child_is_base_class, bool &child_is_deref_of_parent,
      ValueObject *valobj, uint64_t &language_flags) override;

  // Lookup a child given a name. This function will match base class names
  // and member names in "clang_type" only, not descendants.
  uint32_t GetIndexOfChildWithName(void *type, const char *name,
                                   bool omit_empty_base_classes) override;

  // Lookup a child member given a name. This function will match member names
  // only and will descend into "clang_type" children in search for the first
  // member in this class, or any base class that matches "name".
  // TODO: Return all matches for a given name by returning a
  // vector<vector<uint32_t>> so we catch all names that match a given child
  // name, not just the first.
  size_t
  GetIndexOfChildMemberWithName(void *type, const char *name,
                                bool omit_empty_base_classes,
                                std::vector<uint32_t> &child_indexes) override;

  size_t GetNumTemplateArguments(void *type) override;

  lldb::GenericKind GetGenericArgumentKind(void *type, size_t idx);
  CompilerType GetUnboundGenericType(void *type, size_t idx);
  CompilerType GetBoundGenericType(void *type, size_t idx);
  static CompilerType GetGenericArgumentType(CompilerType ct, size_t idx);
  CompilerType GetGenericArgumentType(void *type, size_t idx);

  CompilerType GetTypeForFormatters(void *type) override;

  LazyBool ShouldPrintAsOneLiner(void *type, ValueObject *valobj) override;

  bool IsMeaninglessWithoutDynamicResolution(void *type) override;

  static bool GetSelectedEnumCase(const CompilerType &type,
                                  const DataExtractor &data, ConstString *name,
                                  bool *has_payload, CompilerType *payload,
                                  bool *is_indirect);

  // Dumping types
#ifndef NDEBUG
  /// Convenience LLVM-style dump method for use in the debugger only.
  LLVM_DUMP_METHOD virtual void
  dump(lldb::opaque_compiler_type_t type) const override;
#endif

  void DumpValue(void *type, ExecutionContext *exe_ctx, Stream *s,
                 lldb::Format format, const DataExtractor &data,
                 lldb::offset_t data_offset, size_t data_byte_size,
                 uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                 bool show_types, bool show_summary, bool verbose,
                 uint32_t depth) override;

  bool DumpTypeValue(void *type, Stream *s, lldb::Format format,
                     const DataExtractor &data, lldb::offset_t data_offset,
                     size_t data_byte_size, uint32_t bitfield_bit_size,
                     uint32_t bitfield_bit_offset,
                     ExecutionContextScope *exe_scope,
                     bool is_base_class) override;

  void DumpTypeDescription(void *type) override; // Dump to stdout

  void DumpTypeDescription(void *type, Stream *s) override;

  void DumpTypeDescription(void *type, bool print_help_if_available,
                           bool print_extensions_if_available) override;

  void DumpTypeDescription(void *type, Stream *s, bool print_help_if_available,
                           bool print_extensions_if_available) override;

  // TODO: These methods appear unused. Should they be removed?

  void DumpSummary(void *type, ExecutionContext *exe_ctx, Stream *s,
                   const DataExtractor &data, lldb::offset_t data_offset,
                   size_t data_byte_size) override;

  // TODO: Determine if these methods should move to TypeSystemClang.

  bool IsPointerOrReferenceType(void *type,
                                CompilerType *pointee_type) override;

  llvm::Optional<size_t>
  GetTypeBitAlign(void *type, ExecutionContextScope *exe_scope) override;

  CompilerType GetBuiltinTypeForEncodingAndBitSize(lldb::Encoding encoding,
                                                   size_t bit_size) override {
    return CompilerType();
  }

  bool IsTypedefType(void *type) override;

  // If the current object represents a typedef type, get the underlying type
  CompilerType GetTypedefedType(void *type) override;

  CompilerType GetUnboundType(lldb::opaque_compiler_type_t type);

  std::string GetSuperclassName(const CompilerType &superclass_type);

  CompilerType GetFullyUnqualifiedType(void *type) override;

  CompilerType GetNonReferenceType(void *type) override;

  CompilerType GetLValueReferenceType(void *type) override;

  CompilerType GetRValueReferenceType(void *opaque_type) override;

  uint32_t GetNumDirectBaseClasses(void *opaque_type) override;

  CompilerType GetDirectBaseClassAtIndex(void *opaque_type, size_t idx,
                                         uint32_t *bit_offset_ptr) override;

  bool IsReferenceType(void *type, CompilerType *pointee_type,
                       bool *is_rvalue) override;

  bool
  ShouldTreatScalarValueAsAddress(lldb::opaque_compiler_type_t type) override;

  uint32_t GetNumPointeeChildren(void *type);

  bool IsImportedType(CompilerType type, CompilerType *original_type) override;

  CompilerType GetReferentType(CompilerType compiler_type) override;

  lldb::TypeSP GetCachedType(ConstString mangled) override;

  void SetCachedType(ConstString mangled, const lldb::TypeSP &type_sp) override;

  static bool PerformUserImport(SwiftASTContext &swift_ast_context,
                                SymbolContext &sc,
                                ExecutionContextScope &exe_scope,
                                lldb::StackFrameWP &stack_frame_wp,
                                swift::SourceFile &source_file, Status &error);

  static bool PerformAutoImport(SwiftASTContext &swift_ast_context,
                                SymbolContext &sc,
                                lldb::StackFrameWP &stack_frame_wp,
                                swift::SourceFile *source_file, Status &error);

protected:
  /// This map uses the string value of ConstStrings as the key, and the
  /// TypeBase
  /// * as the value. Since the ConstString strings are uniqued, we can use
  /// pointer equality for string value equality.
  typedef llvm::DenseMap<const char *, swift::TypeBase *>
      SwiftTypeFromMangledNameMap;
  /// Similar logic applies to this "reverse" map
  typedef llvm::DenseMap<swift::TypeBase *, const char *>
      SwiftMangledNameFromTypeMap;

  llvm::TargetOptions *getTargetOptions();

  swift::ModuleDecl *GetScratchModule();

  swift::Lowering::TypeConverter *GetSILTypes();

  swift::SILModule *GetSILModule();

  swift::MemoryBufferSerializedModuleLoader *GetMemoryBufferModuleLoader();

  swift::ModuleDecl *GetCachedModule(const SourceModule &module);

  void CacheDemangledType(ConstString mangled_name,
                          swift::TypeBase *found_type);

  void CacheDemangledTypeFailure(ConstString mangled_name);

  bool LoadOneImage(Process &process, FileSpec &link_lib_spec, Status &error);

  bool LoadLibraryUsingPaths(Process &process, llvm::StringRef library_name,
                             std::vector<std::string> &search_paths,
                             bool check_rpath, StreamString &all_dlopen_errors);

  bool TargetHasNoSDK();

  std::vector<lldb::DataBufferSP> &GetASTVectorForModule(const Module *module);

  CompilerType GetAsClangType(ConstString mangled_name);

  /// Data members.
  /// @{
  TypeSystemSwiftTypeRef m_typeref_typesystem;
  std::unique_ptr<swift::CompilerInvocation> m_compiler_invocation_ap;
  std::unique_ptr<swift::SourceManager> m_source_manager_up;
  std::unique_ptr<swift::DiagnosticEngine> m_diagnostic_engine_ap;
  std::unique_ptr<swift::DWARFImporterDelegate> m_dwarf_importer_delegate_up;
  // CompilerInvocation, SourceMgr, DiagEngine and
  // DWARFImporterDelegate must come before the ASTContext, so they
  // get deallocated *after* the ASTContext.
  std::unique_ptr<swift::ASTContext> m_ast_context_ap;
  std::unique_ptr<llvm::TargetOptions> m_target_options_ap;
  std::unique_ptr<swift::irgen::IRGenerator> m_ir_generator_ap;
  std::unique_ptr<swift::irgen::IRGenModule> m_ir_gen_module_ap;
  llvm::once_flag m_ir_gen_module_once;
  std::unique_ptr<swift::DiagnosticConsumer> m_diagnostic_consumer_ap;
  std::unique_ptr<swift::DependencyTracker> m_dependency_tracker;
  std::unique_ptr<DWARFASTParser> m_dwarf_ast_parser_ap;
  /// A collection of (not necessarily fatal) error messages that
  /// should be printed by Process::PrintWarningCantLoadSwift().
  std::vector<std::string> m_module_import_warnings;
  swift::ModuleDecl *m_scratch_module = nullptr;
  std::unique_ptr<swift::Lowering::TypeConverter> m_sil_types_ap;
  std::unique_ptr<swift::SILModule> m_sil_module_ap;
  /// Owned by the AST.
  swift::MemoryBufferSerializedModuleLoader *m_memory_buffer_module_loader =
      nullptr;
  swift::ClangImporter *m_clang_importer = nullptr;
  SwiftModuleMap m_swift_module_cache;
  SwiftTypeFromMangledNameMap m_mangled_name_to_type_map;
  SwiftMangledNameFromTypeMap m_type_to_mangled_name_map;
  uint32_t m_pointer_byte_size = 0;
  uint32_t m_pointer_bit_align = 0;
  CompilerType m_void_function_type;
  /// Only if this AST belongs to a target will this contain a valid
  /// target weak pointer.
  lldb::TargetWP m_target_wp;
  /// Only if this AST belongs to a target, and an expression has been
  /// evaluated will the target's process pointer be filled in
  lldb_private::Process *m_process = nullptr;
  Module *m_module = nullptr;
  std::string m_platform_sdk_path;

  typedef std::map<Module *, std::vector<lldb::DataBufferSP>> ASTFileDataMap;
  ASTFileDataMap m_ast_file_data_map;
  // FIXME: this vector is needed because the LLDBNameLookup debugger clients
  // are being put into the Module for the SourceFile that we compile the
  // expression into, and so have to live as long as the Module. But it's too
  // late to change swift to get it to take ownership of these DebuggerClients.
  // Since we use the same Target SwiftASTContext for all our compilations,
  // holding them here will keep them alive as long as we need.
  std::vector<std::unique_ptr<swift::DebuggerClient>> m_debugger_clients;
  bool m_initialized_language_options = false;
  bool m_initialized_search_path_options = false;
  bool m_initialized_clang_importer_options = false;
  bool m_reported_fatal_error = false;

  // Whether this is a scratch or a module AST context.
  bool m_is_scratch_context = false;

  Status m_fatal_errors;

  typedef ThreadSafeDenseSet<const char *> SwiftMangledNameSet;
  SwiftMangledNameSet m_negative_type_cache;

  typedef ThreadSafeDenseMap<const char *, lldb::TypeSP> SwiftTypeMap;
  SwiftTypeMap m_swift_type_map;

  /// @}

  /// Record the set of stored properties for each nominal type declaration
  /// for which we've asked this question.
  ///
  /// All of the information in this DenseMap is easily re-constructed
  /// with NominalTypeDecl::getStoredProperties(), but we cache the
  /// result to provide constant-time indexed access.
  llvm::DenseMap<swift::NominalTypeDecl *, std::vector<swift::VarDecl *>>
      m_stored_properties;

  /// Retrieve the stored properties for the given nominal type declaration.
  llvm::ArrayRef<swift::VarDecl *>
  GetStoredProperties(swift::NominalTypeDecl *nominal);

  SwiftEnumDescriptor *GetCachedEnumInfo(void *type);

  friend class CompilerType;

  /// Apply a PathMappingList dictionary on all search paths in the
  /// ClangImporterOptions.
  void RemapClangImporterOptions(const PathMappingList &path_map);

  /// Infer the appropriate Swift resource directory for a target triple.
  llvm::StringRef GetResourceDir(const llvm::Triple &target);

  /// Implementation of \c GetResourceDir.
  static std::string GetResourceDir(llvm::StringRef platform_sdk_path,
                                    llvm::StringRef swift_stdlib_os_dir,
                                    std::string swift_dir,
                                    std::string xcode_contents_path,
                                    std::string toolchain_path,
                                    std::string cl_tools_path);

  /// Return the name of the OS-specific subdirectory containing the
  /// Swift stdlib needed for \p target.
  static llvm::StringRef GetSwiftStdlibOSDir(const llvm::Triple &target,
                                             const llvm::Triple &host);
};

class SwiftASTContextForExpressions : public SwiftASTContext {
  // LLVM RTTI support
  static char ID;

public:
  /// LLVM RTTI support
  /// \{
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || SwiftASTContext::isA(ClassID);
  }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }
  /// \}

  SwiftASTContextForExpressions(std::string description, Target &target);

  virtual ~SwiftASTContextForExpressions() {}

  UserExpression *GetUserExpression(llvm::StringRef expr,
                                    llvm::StringRef prefix,
                                    lldb::LanguageType language,
                                    Expression::ResultType desired_type,
                                    const EvaluateExpressionOptions &options,
                                    ValueObject *ctx_obj) override;

  PersistentExpressionState *GetPersistentExpressionState() override;

private:
  std::unique_ptr<SwiftPersistentExpressionState> m_persistent_state_up;
};

} // namespace lldb_private
#endif // #ifndef liblldb_SwiftASTContext_h_
