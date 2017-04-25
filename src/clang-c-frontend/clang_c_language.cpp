/*******************************************************************\

Module: C++ Language Module

Author: Daniel Kroening, kroening@cs.cmu.edu

\*******************************************************************/

#include "clang_c_language.h"
#include "AST/build_ast.h"

#include <sstream>
#include <fstream>

#include <c2goto/cprover_library.h>
#include <ansi-c/c_preprocess.h>
#include <ansi-c/c_link.h>

#include "clang_c_adjust.h"
#include "clang_c_convert.h"
#include "clang_c_main.h"

languaget *new_clang_c_language()
{
  return new clang_c_languaget;
}

clang_c_languaget::clang_c_languaget()
{
}

std::vector<std::string> clang_c_languaget::get_compiler_args()
{
  std::vector<std::string> compiler_args;
  compiler_args.push_back("clang-tool");

  compiler_args.push_back("-I.");

  // Append mode arg
  switch(config.ansi_c.word_size)
  {
    case 16:
      compiler_args.push_back("-m16");
      break;

    case 32:
      compiler_args.push_back("-m32");
      break;

    case 64:
      compiler_args.push_back("-m64");
      break;

    default:
      std::cerr << "Unknown word size: " << config.ansi_c.word_size
                << std::endl;
      abort();
  }

  if(config.ansi_c.char_is_unsigned)
    compiler_args.push_back("-funsigned-char");

  if(config.options.get_bool_option("deadlock-check"))
  {
    compiler_args.push_back("-Dpthread_join=pthread_join_switch");
    compiler_args.push_back("-Dpthread_mutex_lock=pthread_mutex_lock_check");
    compiler_args.push_back("-Dpthread_mutex_unlock=pthread_mutex_unlock_check");
    compiler_args.push_back("-Dpthread_cond_wait=pthread_cond_wait_check");
  }
  else if (config.options.get_bool_option("lock-order-check"))
  {
    compiler_args.push_back("-Dpthread_join=pthread_join_noswitch");
    compiler_args.push_back("-Dpthread_mutex_lock=pthread_mutex_lock_nocheck");
    compiler_args.push_back("-Dpthread_mutex_unlock=pthread_mutex_unlock_nocheck");
    compiler_args.push_back("-Dpthread_cond_wait=pthread_cond_wait_nocheck");
  }
  else
  {
    compiler_args.push_back("-Dpthread_join=pthread_join_noswitch");
    compiler_args.push_back("-Dpthread_mutex_lock=pthread_mutex_lock_noassert");
    compiler_args.push_back("-Dpthread_mutex_unlock=pthread_mutex_unlock_noassert");
    compiler_args.push_back("-Dpthread_cond_wait=pthread_cond_wait_nocheck");
  }

  for(auto def : config.ansi_c.defines)
    compiler_args.push_back("-D" + def);

  for(auto inc : config.ansi_c.include_paths)
    compiler_args.push_back("-I" + inc);

  // Ignore ctype defined by the system
  compiler_args.push_back("-D__NO_CTYPE");

#ifdef __APPLE__
  compiler_args.push_back("-D_EXTERNALIZE_CTYPE_INLINES_");
  compiler_args.push_back("-D_SECURE__STRING_H_");
  compiler_args.push_back("-U__BLOCKS__");
#endif

  // Force clang see all files as .c
  // This forces the preprocessor to be called even in preprocessed files
  // which allow us to perform transformations using -D
  compiler_args.push_back("-x");
  compiler_args.push_back("c");

  // Add -Wunknown-attributes, preprocessed files with GCC generate a bunch
  // of __leaf__ attributes that we don't care about
  compiler_args.push_back("-Wno-unknown-attributes");

  // Option to avoid creating a linking command
  compiler_args.push_back("-fsyntax-only");

  return compiler_args;
}

bool clang_c_languaget::parse(
  const std::string &path,
  message_handlert &message_handler)
{
  // preprocessing

  std::ostringstream o_preprocessed;
  if(preprocess(path, o_preprocessed, message_handler))
    return true;

  // Generate the compiler arguments and add the file path
  std::vector<std::string> compiler_args = get_compiler_args();
  compiler_args.push_back(path);

  // Get intrinsics
  std::string intrinsics = internal_additions();

  // Get headers
  auto clang_headers = add_clang_headers();

  // Generate ASTUnit and add to our vector
  auto AST =
    buildASTs(
      intrinsics,
      compiler_args,
      clang_headers);

  ASTs.push_back(std::move(AST));

  // Use diagnostics to find errors, rather than the return code.
  for (const auto &astunit : ASTs)
    if (astunit->getDiagnostics().hasErrorOccurred())
      return true;

  return false;
}

bool clang_c_languaget::typecheck(
  contextt& context,
  const std::string& module,
  message_handlert& message_handler)
{
  contextt new_context;

  clang_c_convertert converter(new_context, ASTs);
  if(converter.convert())
    return true;

  clang_c_adjust adjuster(new_context);
  if(adjuster.adjust())
    return true;

  if(c_link(context, new_context, message_handler, module))
    return true;

  // Remove unused
  if(!config.options.get_bool_option("keep-unused"))
    context.remove_unused();

  return false;
}

void clang_c_languaget::show_parse(std::ostream& out __attribute__((unused)))
{
  for (auto &translation_unit : ASTs)
    (*translation_unit).getASTContext().getTranslationUnitDecl()->dump();
}

bool clang_c_languaget::preprocess(
  const std::string &path __attribute__((unused)),
  std::ostream &outstream __attribute__((unused)),
  message_handlert &message_handler __attribute__((unused)))
{
  // TODO: Check the preprocess situation.
#if 0
  return c_preprocess(path, outstream, false, message_handler);
#endif
  return false;
}

bool clang_c_languaget::final(contextt& context, message_handlert& message_handler)
{
  add_cprover_library(context, message_handler);
  return clang_main(context, "c::", "c::main", message_handler);
}

std::string clang_c_languaget::internal_additions()
{
  std::string intrinsics =
    "# 1 \"<esbmc_intrinsics.h>\" 1\n"
    "__attribute__((used))\n"
    "void __ESBMC_assume(_Bool assumption);\n"
    "__attribute__((used))\n"
    "void assert(_Bool assertion);\n"
    "__attribute__((used))\n"
    "void __ESBMC_assert(_Bool assertion, const char *description);\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_same_object(const void *, const void *);\n"

    // pointers
    "__attribute__((used))\n"
    "unsigned __ESBMC_POINTER_OBJECT(const void *p);\n"
    "__attribute__((used))\n"
    "signed __ESBMC_POINTER_OFFSET(const void *p);\n"

    // malloc
    "__attribute__((used))\n"
    "__attribute__((annotate(\"__ESBMC_inf_size\")))\n"
    "_Bool __ESBMC_alloc[1];\n"

    "__attribute__((used))\n"
    "__attribute__((annotate(\"__ESBMC_inf_size\")))\n"
    "_Bool __ESBMC_deallocated[1];\n"

    "__attribute__((used))\n"
    "__attribute__((annotate(\"__ESBMC_inf_size\")))\n"
    "_Bool __ESBMC_is_dynamic[1];\n"

    "__attribute__((used))\n"
    "__attribute__((annotate(\"__ESBMC_inf_size\")))\n"
    "unsigned long __ESBMC_alloc_size[1];\n"

    // float stuff
    "__attribute__((used))\n"
    "_Bool __ESBMC_isnan(double f);\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_isfinite(double f);\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_isinf(double f);\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_isnormal(double f);\n"
    "__attribute__((used))\n"
    "int __ESBMC_rounding_mode = 0;\n"

    // absolute value
    "__attribute__((used))\n"
    "int __ESBMC_abs(int x);\n"
    "__attribute__((used))\n"
    "long int __ESBMC_labs(long int x);\n"
    "__attribute__((used))\n"
    "double __ESBMC_fabs(double x);\n"
    "__attribute__((used))\n"
    "long double __ESBMC_fabsl(long double x);\n"
    "__attribute__((used))\n"
    "float __ESBMC_fabsf(float x);\n"

    // Digital controllers code
    "__attribute__((used))\n"
    "void __ESBMC_generate_cascade_controllers(float * cden, int csize, float * cout, int coutsize, _Bool isDenominator);\n"
    "__attribute__((used))\n"
    "void __ESBMC_generate_delta_coefficients(float a[], double out[], float delta);\n"
    "__attribute__((used))\n"
    "_Bool __ESBMC_check_delta_stability(double dc[], double sample_time, int iwidth, int precision);\n"

    // Forward decs for pthread main thread begin/end hooks. Because they're
    // pulled in from the C library, they need to be declared prior to pulling
    // them in, for type checking.
    "__attribute__((used))\n"
    "void pthread_start_main_hook(void);\n"
    "__attribute__((used))\n"
    "void pthread_end_main_hook(void);\n"

    // Forward declarations for nondeterministic types.
    "int nondet_int();\n"
    "unsigned int nondet_uint();\n"
    "long nondet_long();\n"
    "unsigned long nondet_ulong();\n"
    "short nondet_short();\n"
    "unsigned short nondet_ushort();\n"
    "char nondet_char();\n"
    "unsigned char nondet_uchar();\n"
    "signed char nondet_schar();\n"
    "_Bool nondet_bool();\n"
    "float nondet_float();\n"
    "double nondet_double();"

    // And again, for TACAS VERIFIER versions,
    "int __VERIFIER_nondet_int();\n"
    "unsigned int __VERIFIER_nondet_uint();\n"
    "long __VERIFIER_nondet_long();\n"
    "unsigned long __VERIFIER_nondet_ulong();\n"
    "short __VERIFIER_nondet_short();\n"
    "unsigned short __VERIFIER_nondet_ushort();\n"
    "char __VERIFIER_nondet_char();\n"
    "unsigned char __VERIFIER_nondet_uchar();\n"
    "signed char __VERIFIER_nondet_schar();\n"
    "_Bool __VERIFIER_nondet_bool();\n"
    "float __VERIFIER_nondet_float();\n"
    "double __VERIFIER_nondet_double();"

    "\n";

  return intrinsics;
}

bool clang_c_languaget::from_expr(
  const exprt &expr __attribute__((unused)),
  std::string &code __attribute__((unused)),
  const namespacet &ns __attribute__((unused)))
{
  std::cout << "Method " << __PRETTY_FUNCTION__ << " not implemented yet" << std::endl;
  abort();
  return true;
}

bool clang_c_languaget::from_type(
  const typet &type __attribute__((unused)),
  std::string &code __attribute__((unused)),
  const namespacet &ns __attribute__((unused)))
{
  std::cout << "Method " << __PRETTY_FUNCTION__ << " not implemented yet" << std::endl;
  abort();
  return true;
}

bool clang_c_languaget::to_expr(
  const std::string &code __attribute__((unused)),
  const std::string &module __attribute__((unused)),
  exprt &expr __attribute__((unused)),
  message_handlert &message_handler __attribute__((unused)),
  const namespacet &ns __attribute__((unused)))
{
  std::cout << "Method " << __PRETTY_FUNCTION__ << " not implemented yet" << std::endl;
  abort();
  return true;
}
