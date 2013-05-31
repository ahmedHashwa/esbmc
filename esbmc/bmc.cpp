/*******************************************************************\

Module: Symbolic Execution of ANSI-C

Authors: Daniel Kroening, kroening@kroening.com
         Lucas Cordeiro, lcc08r@ecs.soton.ac.uk

\*******************************************************************/

#include <sys/types.h>

#include <signal.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <winbase.h>
#undef ERROR
#undef small
#endif

#include <sstream>
#include <fstream>

#include <irep2.h>
#include <i2string.h>
#include <location.h>
#include <time_stopping.h>
#include <message_stream.h>
#include <migrate.h>

#include <langapi/mode.h>
#include <langapi/languages.h>
#include <langapi/language_util.h>

#include <goto-symex/goto_trace.h>
#include <goto-symex/build_goto_trace.h>
#include <goto-symex/slice.h>
#include <goto-symex/slice_by_trace.h>
#include <goto-symex/xml_goto_trace.h>
#include <goto-symex/reachability_tree.h>

#include "bmc.h"
#include "document_subgoals.h"
#include "version.h"

static volatile bool checkpoint_sig = false;

void
sigusr1_handler(int sig __attribute__((unused)))
{

  checkpoint_sig = true;
  return;
}

/*******************************************************************\

Function: bmct::do_cbmc

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void bmct::do_cbmc(prop_convt &solver, symex_target_equationt &equation)
{
  solver.set_message_handler(message_handler);

  equation.convert(solver);

  // After all conversions, clear cache, which tends to contain a large
  // amount of stuff.
  solver.clear_cache();
}

/*******************************************************************\

Function: bmct::error_trace

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void bmct::error_trace(prop_convt &prop_conv,
                       symex_target_equationt &equation)
{
  status("Building error trace");

  goto_tracet goto_trace;
  build_goto_trace(equation, prop_conv, goto_trace);

  goto_trace.metadata_filename = options.get_option("llvm-metadata");

  switch(ui)
  {
  case ui_message_handlert::PLAIN:
    std::cout << std::endl << "Counterexample:" << std::endl;
    show_goto_trace(std::cout, ns, goto_trace);
    break;

  case ui_message_handlert::OLD_GUI:
    show_goto_trace_gui(std::cout, ns, goto_trace);
    break;

  case ui_message_handlert::XML_UI:
    {
      xmlt xml;
      convert(ns, goto_trace, xml);
      std::cout << xml << std::endl;
    }
    break;

  default:
    assert(false);
  }
}

/*******************************************************************\

Function: bmct::run_decision_procedure

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

prop_convt::resultt
bmct::run_decision_procedure(prop_convt &prop_conv,
                             symex_target_equationt &equation)
{
  static bool first_uw=false;
  std::string logic;

  if (options.get_bool_option("bl-bv") || options.get_bool_option("z3-bv") ||
      !options.get_bool_option("int-encoding"))
    logic = "bit-vector arithmetic";
  else
    logic = "integer/real arithmetic";

  if (!options.get_bool_option("smt") && !options.get_bool_option("btor"))
    std::cout << "Encoding remaining VCC(s) using " << logic << "\n";

  prop_conv.set_message_handler(message_handler);
  prop_conv.set_verbosity(get_verbosity());

  fine_timet encode_start = current_time();
  do_cbmc(prop_conv, equation);
  fine_timet encode_stop = current_time();

  if (!options.get_bool_option("smt") && !options.get_bool_option("btor"))
  {
    std::ostringstream str;
    str << "Encoding to solver time: ";
    output_time(encode_stop - encode_start, str);
    str << "s";
    status(str.str());
  }

  fine_timet sat_start=current_time();
  prop_convt::resultt dec_result=prop_conv.dec_solve();
  fine_timet sat_stop=current_time();

  // output runtime
  if (!options.get_bool_option("smt") && !options.get_bool_option("btor"))
  {
    std::ostringstream str;
    str << "Runtime decision procedure: ";
    output_time(sat_stop-sat_start, str);
    str << "s";
    status(str.str());
  }

  if(options.get_bool_option("uw-model") && first_uw)
  {
    std::cout << "number of assumptions: " << _number_of_assumptions << " literal(s)"<< std::endl;
    std::cout << "size of the unsatisfiable core: " << _unsat_core << " literal(s)"<< std::endl;
  }
  else
    first_uw=true;

  return dec_result;
}

/*******************************************************************\

Function: bmct::report_success

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void bmct::report_success()
{

  if(options.get_bool_option("base-case"))
  {
    status("No bug has been found in the base case");
    return ;
  }

  status("VERIFICATION SUCCESSFUL");

  switch(ui)
  {
  case ui_message_handlert::OLD_GUI:
    std::cout << "SUCCESS" << std::endl
              << "Verification successful" << std::endl
              << ""     << std::endl
              << ""     << std::endl
              << ""     << std::endl
              << ""     << std::endl;
    break;

  case ui_message_handlert::PLAIN:
    break;

  case ui_message_handlert::XML_UI:
    {
      xmlt xml("cprover-status");
      xml.data="SUCCESS";
      std::cout << xml;
      std::cout << std::endl;
    }
    break;

  default:
    assert(false);
  }

}

/*******************************************************************\

Function: bmct::report_failure

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void bmct::report_failure()
{
  status("VERIFICATION FAILED");

  switch(ui)
  {
  case ui_message_handlert::OLD_GUI:
    break;

  case ui_message_handlert::PLAIN:
    break;

  case ui_message_handlert::XML_UI:
    {
      xmlt xml("cprover-status");
      xml.data="FAILURE";
      std::cout << xml;
      std::cout << std::endl;
    }
    break;

  default:
    assert(false);
  }
}

/*******************************************************************\

Function: bmct::show_program

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void bmct::show_program(symex_target_equationt &equation)
{
  unsigned count=1;

  languagest languages(ns, MODE_C);

  std::cout << std::endl << "Program constraints:" << std::endl;

  for(symex_target_equationt::SSA_stepst::const_iterator
      it=equation.SSA_steps.begin();
      it!=equation.SSA_steps.end(); it++)
  {
    if(it->is_assignment())
    {
      std::string string_value;
      languages.from_expr(migrate_expr_back(it->cond), string_value);
      std::cout << "(" << count << ") " << string_value << std::endl;
      count++;
    }
#if 1
    else if(it->is_assert())
    {
      std::string string_value;
      languages.from_expr(migrate_expr_back(it->cond), string_value);
      std::cout << "(" << count << ") " << "(assert)" << string_value << std::endl;
      count++;
    }
    else if(it->is_assume())
    {
      std::string string_value;
      languages.from_expr(migrate_expr_back(it->cond), string_value);
      std::cout << "(" << count << ") " << "(assume)" << string_value << std::endl;
      count++;
    }
#
#endif
  }
}

/*******************************************************************\

Function: bmct::run

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool bmct::run(void)
{
#ifndef _WIN32
  struct sigaction act;
#endif
  bool resp;

#ifndef _WIN32
  // Collect SIGUSR1, indicating that we're supposed to checkpoint.
  act.sa_handler = sigusr1_handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGUSR1, &act, NULL);
#endif

  symex->options.set_option("unwind", options.get_option("unwind"));
  symex->setup_for_new_explore();

  if(options.get_bool_option("schedule"))
  {
    if(options.get_bool_option("uw-model"))
        std::cout << "*** UW loop " << ++uw_loop << " ***" << std::endl;

    resp = run_thread();


    //underapproximation-widening model
    while (_unsat_core)
    {
      std::cout << "*** UW loop " << ++uw_loop << " ***" << std::endl;
      resp = run_thread();
    }

    return resp;
  }
  else
  {
    if (options.get_bool_option("from-checkpoint")) {
      if (options.get_option("checkpoint-file") == "") {
        std::cerr << "Please provide a checkpoint file" << std::endl;
        abort();
      }

      reachability_treet::dfs_position pos(
                                         options.get_option("checkpoint-file"));
      symex->restore_from_dfs_state((void*)&pos);
    }

    do
    {
      if(!options.get_bool_option("k-induction"))
        if (++interleaving_number>1) {
    	  print(8, "*** Thread interleavings "+
    	           i2string((unsigned long)interleaving_number)+
    	           " ***");
        }

      fine_timet bmc_start = current_time();
      if(run_thread())
      {
        ++interleaving_failed;

        if (options.get_bool_option("checkpoint-on-cex")) {
          write_checkpoint();
        }

        if(!options.get_bool_option("all-runs"))
        {
          return true;
        }
      }
      fine_timet bmc_stop = current_time();

      std::ostringstream str;
      str << "BMC program time: ";
      output_time(bmc_stop-bmc_start, str);
      str << "s";
      status(str.str());

      if (checkpoint_sig) {
        write_checkpoint();
      }

      // Only run for one run
      if (options.get_bool_option("interactive-ileaves"))
        return false;

    } while(symex->setup_next_formula());
  }

  if (options.get_bool_option("all-runs"))
  {
    std::cout << "*** number of generated interleavings: " << interleaving_number << " ***" << std::endl;
    std::cout << "*** number of failed interleavings: " << interleaving_failed << " ***" << std::endl;
  }

  if (options.get_bool_option("ltl")) {
    // So, what was the lowest value ltl outcome that we saw?
    if (ltl_results_seen[ltl_res_bad]) {
      std::cout << "Final lowest outcome: LTL_BAD" << std::endl;
      return false;
    } else if (ltl_results_seen[ltl_res_failing]) {
      std::cout << "Final lowest outcome: LTL_FAILING" << std::endl;
      return false;
    } else if (ltl_results_seen[ltl_res_succeeding]) {
      std::cout << "Final lowest outcome: LTL_SUCCEEDING" << std::endl;
      return false;
    } else if (ltl_results_seen[ltl_res_good]) {
      std::cout << "Final lowest outcome: LTL_GOOD" << std::endl;
      return false;
    } else {
      std::cout << "No traces seen, apparently" << std::endl;
      return false;
    }
  }

  return false;
}

bool bmct::run_thread()
{
  goto_symext::symex_resultt *result;
  solver_base *solver;
  symex_target_equationt *equation;
  bool ret;

  try
  {
    if(options.get_bool_option("schedule"))
    {
      result = symex->generate_schedule_formula();
    }
    else
    {
      result = symex->get_next_formula();
    }
  }

  catch(std::string &error_str)
  {
    message_streamt message_stream(*get_message_handler());
    message_stream.error(error_str);
    return true;
  }

  catch(const char *error_str)
  {
    message_streamt message_stream(*get_message_handler());
    message_stream.error(error_str);
    return true;
  }

  equation = dynamic_cast<symex_target_equationt*>(result->target);

  print(8, "size of program expression: "+
           i2string((unsigned long)equation->SSA_steps.size())+
           " assignments");

  if (options.get_bool_option("double-assign-check")) {
    equation->check_for_duplicate_assigns();
  }

  try
  {
    if(options.get_option("slice-by-trace")!="")
    {
      symex_slice_by_tracet symex_slice_by_trace;
      symex_slice_by_trace.slice_by_trace
      (options.get_option("slice-by-trace"), *equation);
    }

    if(!options.get_bool_option("no-slice"))
    {
      slice(*equation);
    }
    else
    {
      simple_slice(*equation);
    }

    if (options.get_bool_option("program-only") ||
        options.get_bool_option("program-too"))
      show_program(*equation);

    if (options.get_bool_option("program-only"))
      return false;

    {
      std::string msg;
      msg="Generated "+i2string(result->total_claims)+
          " VCC(s), "+i2string(result->remaining_claims)+
          " remaining after simplification";
      print(8, msg);
    }

    if(options.get_bool_option("document-subgoals"))
    {
      document_subgoals(*equation, std::cout);
      return false;
    }

    if(options.get_bool_option("show-vcc"))
    {
      show_vcc(*equation);
      return false;
    }

    if(result->remaining_claims==0)
    {
      report_success();
      return false;
    }

    if (options.get_bool_option("ltl")) {
      int res = ltl_run_thread(equation);
      // Record that we've seen this outcome; later decide what the least
      // outcome was.
      ltl_results_seen[res]++;
      return false;
    }

    if (options.get_bool_option("smt"))
      if (interleaving_number !=
          (unsigned int) strtol(options.get_option("smtlib-ileave-num").c_str(), NULL, 10))
        return false;

    if(options.get_bool_option("z3")) {
#ifdef Z3
      if (options.get_bool_option("smt-during-symex")) {
        solver = new z3_runtime_solver(*this, is_cpp, runtime_z3_conv);
      } else {
        solver = new z3_solver(*this, is_cpp, ns);
      }
#else
      throw "This version of ESBMC was not compiled with Z3 support";
#endif
    } else {
      // If we have Z3, default to Z3. Otherwise, user needs to explicitly
      // select an SMT solver
#ifdef Z3
      solver = new z3_solver(*this, is_cpp, ns);
#else
      throw "Please specify a SAT/SMT solver to use";
#endif
    }

    ret = solver->run_solver(*equation);
    delete solver;
    return ret;
  }

  catch(std::string &error_str)
  {
    error(error_str);
    return true;
  }

  catch(const char *error_str)
  {
    error(error_str);
    return true;
  }
}

int
bmct::ltl_run_thread(symex_target_equationt *equation)
{
  solver_base *solver;
  bool ret;
  unsigned int num_asserts = 0;
#ifndef Z3
  std::cerr << "Can't run LTL checking without Z3 compiled in" << std::endl;
  exit(1);
#else
  // LTL checking - first check for whether we have an indeterminate prefix,
  // and then check for all others.

  // Start by turning all assertions that aren't the negative prefix
  // assertion into skips.
  for(symex_target_equationt::SSA_stepst::iterator
      it=equation->SSA_steps.begin();
      it!=equation->SSA_steps.end(); it++)
  {
    if (it->is_assert()) {
      if (it->comment != "LTL_BAD") {
        it->type = goto_trace_stept::SKIP;
      } else {
        num_asserts++;
      }
    }
  }

  std::cout << "Checking for LTL_BAD" << std::endl;
  if (num_asserts != 0) {
    solver = new z3_solver(*this, is_cpp, ns);
    ret = solver->run_solver(*equation);
    delete solver;
    if (ret) {
      std::cout << "Found trace satisfying LTL_BAD" << std::endl;
      return ltl_res_bad;
    }
  } else {
    std::cerr << "Warning: Couldn't find LTL_BAD assertion" << std::endl;
  }

  // Didn't find it; turn skip steps back into assertions.
  for(symex_target_equationt::SSA_stepst::iterator
      it=equation->SSA_steps.begin();
      it!=equation->SSA_steps.end(); it++)
  {
    if (it->type == goto_trace_stept::SKIP)
      it->type = goto_trace_stept::ASSERT;
  }

  // Try again, with LTL_FAILING
  num_asserts = 0;
  for(symex_target_equationt::SSA_stepst::iterator
      it=equation->SSA_steps.begin();
      it!=equation->SSA_steps.end(); it++)
  {
    if (it->is_assert()) {
      if (it->comment != "LTL_FAILING") {
        it->type = goto_trace_stept::SKIP;
      } else {
        num_asserts++;
      }
    }
  }

  std::cout << "Checking for LTL_FAILING" << std::endl;
  if (num_asserts != 0) {
    solver = new z3_solver(*this, is_cpp, ns);
    ret = solver->run_solver(*equation);
    delete solver;
    if (ret) {
      std::cout << "Found trace satisfying LTL_FAILING" << std::endl;
      return ltl_res_failing;
    }
  } else {
    std::cerr << "Warning: Couldn't find LTL_FAILING assertion" <<std::endl;
  }

  // Didn't find it; turn skip steps back into assertions.
  for(symex_target_equationt::SSA_stepst::iterator
      it=equation->SSA_steps.begin();
      it!=equation->SSA_steps.end(); it++)
  {
    if (it->type == goto_trace_stept::SKIP)
      it->type = goto_trace_stept::ASSERT;
  }

  // Try again, with LTL_SUCCEEDING
  num_asserts = 0;
  for(symex_target_equationt::SSA_stepst::iterator
      it=equation->SSA_steps.begin();
      it!=equation->SSA_steps.end(); it++)
  {
    if (it->is_assert()) {
      if (it->comment != "LTL_SUCCEEDING") {
        it->type = goto_trace_stept::SKIP;
      } else {
        num_asserts++;
      }
    }
  }

  std::cout << "Checking for LTL_SUCCEEDING" << std::endl;
  if (num_asserts != 0) {
    solver = new z3_solver(*this, is_cpp, ns);
    ret = solver->run_solver(*equation);
    delete solver;
    if (ret) {
      std::cout << "Found trace satisfying LTL_SUCCEEDING" << std::endl;
      return ltl_res_succeeding;
    }
  } else {
    std::cerr << "Warning: Couldn't find LTL_SUCCEEDING assertion"
              << std::endl;
  }

  // Otherwise, we just got a good prefix.
  for(symex_target_equationt::SSA_stepst::iterator
      it=equation->SSA_steps.begin();
      it!=equation->SSA_steps.end(); it++)
  {
    if (it->type == goto_trace_stept::SKIP)
      it->type = goto_trace_stept::ASSERT;
  }

  return ltl_res_good;
#endif
}

bool bmct::solver_base::run_solver(symex_target_equationt &equation)
{
  switch(bmc.run_decision_procedure(*conv, equation))
  {
    case prop_convt::P_UNSATISFIABLE:
      if(!bmc.options.get_bool_option("base-case"))
        bmc.report_success();
      else
        bmc.status("No bug has been found in the base case");
      return false;

    case prop_convt::P_SATISFIABLE:
      if (bmc.options.get_bool_option("inductive-step") &&
    		  bmc.options.get_bool_option("show-counter-example"))
      {
        bmc.error_trace(*conv, equation);
   	    bmc.report_failure();
   	    return false;
      }
      else if(!bmc.options.get_bool_option("inductive-step")
    		  && !bmc.options.get_bool_option("forward-condition"))
      {
        bmc.error_trace(*conv, equation);
   	    bmc.report_failure();
      }
      else if (bmc.options.get_bool_option("forward-condition"))
        bmc.status("The forward condition is unable to prove the property");
      else
        bmc.status("The inductive step is unable to prove the property");

      return true;

  // Return failure if we didn't actually check anything, we just emitted the
  // test information to an SMTLIB formatted file. Causes esbmc to quit
  // immediately (with no error reported)
  case prop_convt::P_SMTLIB:
    return true;

  default:
    bmc.error("decision procedure failed");
    return true;
  }
}

#ifdef Z3
bmct::z3_solver::z3_solver(bmct &bmc, bool is_cpp, const namespacet &ns)
  : solver_base(bmc), z3_conv(bmc.options.get_bool_option("uw-model"),
                               bmc.options.get_bool_option("int-encoding"),
                               bmc.options.get_bool_option("smt"),
                               is_cpp, ns)
{
  z3_conv.set_filename(bmc.options.get_option("outfile"));
  z3_conv.set_z3_core_size(atol(bmc.options.get_option("core-size").c_str()));
  conv = &z3_conv;
}

bool bmct::z3_solver::run_solver(symex_target_equationt &equation)
{
  bool result = bmct::solver_base::run_solver(equation);
  bmc._unsat_core = z3_conv.get_z3_core_size();
  bmc._number_of_assumptions = z3_conv.get_z3_number_of_assumptions();
  return result;
}

bmct::z3_runtime_solver::z3_runtime_solver(bmct &bmc,
                                           bool is_cpp __attribute__((unused)),
                                           z3_convt *c)
  : solver_base(bmc), z3_conv(c)
{
  conv = c;
}

bool bmct::z3_runtime_solver::run_solver(symex_target_equationt &equation)
{
  bool result = bmct::solver_base::run_solver(equation);
  bmc._unsat_core = z3_conv->get_z3_core_size();
  bmc._number_of_assumptions = z3_conv->get_z3_number_of_assumptions();
  return result;
}

#endif

bmct::output_solver::output_solver(bmct &bmc)
  : solver_base(bmc)
{

  const std::string &filename = bmc.options.get_option("outfile");

  if (filename.empty() || filename=="-") {
    out_file = &std::cout;
  } else {
    std::ofstream *out = new std::ofstream(filename.c_str());
    out_file = out;

    if (!out_file)
    {
      std::cerr << "failed to open " << filename << std::endl;
      delete out_file;
      return;
    }
  }

  *out_file << "%%%\n";
  *out_file << "%%% Generated by ESBMC " << ESBMC_VERSION << "\n";
  *out_file << "%%%\n\n";

  return;
}

bmct::output_solver::~output_solver()
{

  if (out_file != &std::cout)
    delete out_file;
  return;
}

bool bmct::output_solver::run_solver(symex_target_equationt &equation)
{

  bmc.do_cbmc(*conv, equation);
  conv->dec_solve();
  return write_output();
}

void bmct::write_checkpoint(void)
{
  std::string f;

  if (options.get_option("checkpoint-file") == "") {
    char buffer[32];
#ifndef _WIN32
    pid_t pid = getpid();
#else
    unsigned long pid = GetCurrentProcessId();
#endif
    sprintf(buffer, "%d", pid);
    f = "esbmc_checkpoint." + std::string(buffer);
  } else {
    f = options.get_option("checkpoint-file");
  }

  symex->save_checkpoint(f);
  return;
}
