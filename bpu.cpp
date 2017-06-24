//Tsalesis Evangelos
//AM: 1779
/*! @file
 *  This is a Branch Target Buffer (BTB) Simulator using the PIN tool
 */

#include "pin.H"
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <fstream>


// --------------------------
// Replace with your own predictor 
//#include "StaticPredictor.h"
//StaticPredictor myBPU; //(2048, 4);
// --------------------------

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "btb.out", "specify output file name for BTB simulator");

KNOB<UINT32> KnobMispredRate(KNOB_MODE_WRITEONCE, "pintool",
    "mpr", "20", "specify direction misprediction rate for BTB simulator");


// Add your KNOBs here
///////////////////////////////////////////////////////////
KNOB<UINT64> KnobBTBsize(KNOB_MODE_WRITEONCE, "pintool",
    "btbs", "1024", "specify BTB size");

KNOB<UINT64> KnobBTBassoc(KNOB_MODE_WRITEONCE, "pintool",
    "btba", "4", "specify BTB associativity");

KNOB<UINT64> KnobBTBTagSize(KNOB_MODE_WRITEONCE, "pintool",
    "tags", "12", "specify BTB tag size");

KNOB<UINT64> KnobRASsize(KNOB_MODE_WRITEONCE, "pintool",
    "ras", "10", "specify RAS size");
		
///////////////////////////////////////////////////////////

/* ===================================================================== */
// Branch Target Buffer object & simulation methods
/* ===================================================================== */
class BPU {
//Added class variables
///////////////////////////////
typedef struct btb_entry {
	UINT64 tag;
	bool FlagIsReturn;
	ADDRINT BTA;
	btb_entry* next;
} BTB_ENTRY;

BTB_ENTRY** BTB;

UINT64 BTBSetSize;
UINT64 BTBNumberOfSets;

ADDRINT* RAS; 
UINT64 topRAS;
UINT64 RASsize;

///////////////////////////////

public:
BPU();  // Make your own constructor!

bool PredictDirection(ADDRINT PC,
                      bool isControlFlow,
                      bool brTaken);

ADDRINT PredictTarget(ADDRINT PC,
                      ADDRINT fallThroughAddr,
                      bool predictDir);

VOID UpdatePredictor(ADDRINT PC,         // address of instruction executing now
                     bool brTaken,       // the actual direction
                     ADDRINT targetPC,   // the next PC, **if taken**
                     ADDRINT returnAddr, // return address for subroutine calls,
                     bool isCall,        // is a subroutine call
                     bool isReturn,      // is a return from subroutine
                     bool correctDir,    // my direction prediction was correct
                     bool correctTarg);  // my target prediction was correct

std::string ReportCounters();

}; // end class BPU 

/*!
 * Predicts the direction of instruction at address PC: true - branch is taken 
 *   Not a real predictor: randomly makes wrong predictions
 *   only for control flow instructions
 * This function is called for every instruction executed.
 * @param[in]   PC              address of current instruction
 * @param[in]   isControlFlow   true if instruction may change control flow
 * @param[in]   brTaken         true if instruction takes a branch
 */
bool BPU::PredictDirection(ADDRINT PC,
                           bool isControlFlow,
                           bool brTaken)
{
    if (!isControlFlow) {
        // CHEAT: can't do this in real hardware!
        // Don't make wrong predictions on normal instructions!
        // Phantom branches may appear if this is modified:
        //  normal instructions mistaken for branches because they are predicted
        //  taken and have a target address in the BTB....
        return brTaken; // should be false.
    }
    // --------------------------
    // Get a random number up to 100
    // http://stackoverflow.com/questions/822323/how-to-generate-a-random-number-in-c

    // Chop off all of the values that would cause skew...
    long end = RAND_MAX / 100; // truncate skew
    end *= 100;

    // ... and ignore results from rand() that fall above that limit.
    // (Worst case the loop condition should succeed 50% of the time,
    // so we can expect to bail out of this loop pretty quickly.)
    UINT32 r;
    while ((r = rand()) >= end)
        ;

    // -------------------------
    if (r % 100 > (100-KnobMispredRate.Value()))
        return !brTaken;  // mispredict direction
    return brTaken;
}


/*!
// Initialize data structures for branch predictors here.
// @note  Use KNOBs to pass parameters to the BPU, such as:
//   number of entries, associativity, RAS entries, replacement policies, ...
 */

// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////
BPU::BPU(void) {
	BTBNumberOfSets = KnobBTBsize.Value()/KnobBTBassoc.Value();	
	BTBSetSize = KnobBTBassoc.Value();
	RASsize = KnobRASsize.Value();

	//BTB: array of linked lists (sets)
	BTB = (BTB_ENTRY**) malloc(BTBNumberOfSets*sizeof(BTB_ENTRY*));
	for (UINT64 i=0; i<BTBNumberOfSets; i++){
		BTB[i] = NULL;
	}
	
	//RAS: array of instruction addresses
	RAS = (ADDRINT*) malloc(RASsize*sizeof(ADDRINT));
	topRAS = -1;
}


/*!
// Predict the target of the instruction at address PC by looking it up in 
//  the BTB.  Use the direction prediction predictDir to decide between the
//  target address in the BTB or the fallThroughAddr 
 * @param[in]   PC              address of current instruction
 * @param[in]   fallThroughAddr address of next, sequential instruction
 * @param[in]   predictDir      the predicted direction of this "branch"
 */
ADDRINT BPU::PredictTarget(ADDRINT PC,
                           ADDRINT fallThroughAddr,
                           bool predictDir)
{
	if (!predictDir) {    
		return fallThroughAddr;
	}
	
	UINT64 index = PC & (BTBNumberOfSets-1);
	UINT64 tag = (PC/BTBNumberOfSets) & ((1 << KnobBTBTagSize.Value()) - 1);
	
	BTB_ENTRY* BTBEntryCurrent = BTB[index];

	//find tag in the set
	while (BTBEntryCurrent != NULL){
		if (BTBEntryCurrent->tag == tag){
			if (BTBEntryCurrent->FlagIsReturn == true){				//if isReturn, pop a RAS entry
				ADDRINT temp = RAS[topRAS];
				topRAS = ((topRAS == 0) ? RASsize-1 : topRAS - 1);
				return temp;
			}
			return BTBEntryCurrent->BTA;
		}
		BTBEntryCurrent = BTBEntryCurrent->next;
	}
	return fallThroughAddr;
}

/*!
// Update the information in the BTB, RAS for the branch instruction
// at address PC, using the fully available information now that it
// has been executed.
 * @param[in]   PC           address of current instruction
 * @param[in]   brTaken      true if is actually taken
 * @param[in]   targetPC     the target, if it is taken
 * @param[in]   returnAddr   the return address, if it is a subroutine call
 * @param[in]   isCall       true if this is a subroutine call
 * @param[in]   isReturn     true if this is a subroutine return
 * @param[in]   correctDir   true if the direction was predicted correctly
 * @param[in]   correctTarg  true if the target was predicted correctly
// @note Use KNOBs to pass parameters related to BTB prediction such as
//   replacement policies, when to insert an entry, ...
*/
VOID BPU::UpdatePredictor(ADDRINT PC,           // address of instruction executing now
                          bool brTaken,      // the actual direction
                          ADDRINT targetPC,     // the next PC, **if taken**
                          ADDRINT returnAddr,   // return address for subroutine calls,
                          bool isCall,       // is a subroutine call
                          bool isReturn,     // is a return from subroutine
                          bool correctDir,   // my direction prediction was correct
                          bool correctTarg)  // my target prediction was correct
{
	UINT64 counter = 0;  
		
	UINT64 index = PC & (BTBNumberOfSets-1);
	UINT64 tag = (PC/BTBNumberOfSets) & ((1 << KnobBTBTagSize.Value()) - 1);

	BTB_ENTRY* BTBEntryCurrent = BTB[index];
	BTB_ENTRY* BTBEntryPrevious = NULL;

	//Push a RAS entry
	if (isCall){
		topRAS = (topRAS + 1) % RASsize;
		RAS[topRAS] = returnAddr;
	}
	
	//Update BTB
	if (brTaken && !correctTarg){
		while (BTBEntryCurrent != NULL){
			counter++;
			
			//Update an existing entry
			if (BTBEntryCurrent->tag == tag){
				BTBEntryCurrent->BTA = targetPC;
				return;
			}
			
			//Replace the only entry when the BTB is direct-mapped
			if (BTBSetSize == 1){	
				BTBEntryCurrent->tag = tag;
				if (isReturn){
					BTBEntryCurrent->FlagIsReturn = true;
				}
				else{
					BTBEntryCurrent->FlagIsReturn = false;
				}
				BTBEntryCurrent->BTA = targetPC;
				return;
			}
			
			//Add a new entry in a set
			if (BTBEntryCurrent->next == NULL){	
				//Add new entry as the head of the set	
				BTB_ENTRY* BTBNewEntry = (BTB_ENTRY*) malloc(sizeof(BTB_ENTRY));
				BTBNewEntry->tag = tag;
				if (isReturn){
					BTBNewEntry->FlagIsReturn = true;
				}
				else{
					BTBNewEntry->FlagIsReturn = false;
				}
				BTBNewEntry->BTA = targetPC;
				BTBNewEntry->next = BTB[index];
				
				BTB[index] = BTBNewEntry;
					
				//Remove last entry when set is full
				if (counter == BTBSetSize){
					BTBEntryPrevious->next = NULL;
					free(BTBEntryCurrent);
				}
				return;	
			}
			BTBEntryPrevious = BTBEntryCurrent;
			BTBEntryCurrent = BTBEntryCurrent->next;
		}
		
		//Add first entry in a set
		BTB_ENTRY* BTBNewEntry = (BTB_ENTRY*) malloc(sizeof(BTB_ENTRY));
		BTBNewEntry->tag = tag;
		if (isReturn){
			BTBNewEntry->FlagIsReturn = true;
		}
		else {		
			BTBNewEntry->FlagIsReturn = false;
		}
		BTBNewEntry->BTA = targetPC;
		BTBNewEntry->next = NULL;
		
		BTB[index] = BTBNewEntry;
	}
	return;
}
////////////////////////////////////////////////////////////////////////////////

std::string BPU::ReportCounters()
{
    return std::string();
}

/* ================================================================== */
// Global variables 
/* ================================================================== */
BPU  *myBPU; // The Branch Prediction Unit
std::ofstream *outFile;   // File for simulation output

// Global counters for the simulator:
static UINT64 cnt_instr = 0;
static UINT64 cnt_branches = 0;
static UINT64 cnt_branches_taken = 0;
static UINT64 cnt_correctPredDir = 0;
static UINT64 cnt_correctPredTarg = 0;
static UINT64 cnt_correctPred     = 0;

//extra statistics
//////////////////////////////////////////////////////////
//static UINT64 isCallCounter = 0;
//static UINT64 isReturnCounter = 0;
//////////////////////////////////////////////////////////

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    cerr << "This tool is a Branch Targer Simulator." << endl <<
            "It runs an application and prints out the number of" << endl <<
            "dynamically executed branches, their target prediction ratio," << endl <<
            "and other metrics." << endl << endl;
    cerr << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}


/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

/*!
 * Process branches: predict all instructions at Fetch, check prediction
 *  and update prediction structures at Execute stage
 * This function is called for every instruction executed.
 * @param[in]   PC              address of current instruction
 * @param[in]   targetPC        the next PC, **if taken**
 * @param[in]   brTaken         the branch direction, 0 - not taken, 1 - taken
 * @param[in]   size            the instruction size in bytes
 * @param[in]   isCall          true if the instruction is a subroutine call
 * @param[in]   isReturn        true if the instruction is a subroutine return
 * @param[in]   isConstrolFlow  true if the instruction is branch, jump, return, ...
 */
VOID ProcessBranch(ADDRINT PC,
                   ADDRINT targetPC,
                   bool brTaken,
                   UINT32 size,
                   bool isCall,
                   bool isReturn,
                   bool isControlFlow)
{
   /*
   *outFile << "PC: "          << PC 
            << " targetPC: "   << targetPC 
            << " taken: "      << brTaken
            << " isCall: "     << isCall
            << " isRet: "      << isReturn
            << " isBrOrCall: " << isControlFlow
            << " PC+size: " << PC+size
           << endl;
    */
    ADDRINT fallThroughAddr = PC + size;
    bool    correctDir  = false;
    bool    correctTarg = false;
    bool    predictDir;
    ADDRINT predictPC;

    // ------------------------------------------
    // Make your prediction:  (@ Fetch stage)
    predictDir = myBPU->PredictDirection(PC, isControlFlow, brTaken);
    predictPC  = myBPU->PredictTarget(PC, fallThroughAddr, predictDir);
    // ------------------------------------------


    // ------------------------------------------
    // Update counters, check prediction
    cnt_instr++;
    if (isControlFlow) {
        cnt_branches++; 
        if (brTaken)
            cnt_branches_taken++;
    }

    if (predictDir == brTaken) { // Correct prediction of branch direction
        correctDir = true;
        if (isControlFlow) {
            cnt_correctPredDir++; // Count correct predictions for actual branches
        }
    }

    if (brTaken) { // brach was actually taken
        if (predictPC == targetPC) { // Target predicted
            correctTarg = true;
            if (isControlFlow) {
                cnt_correctPredTarg++;
            }
        }
    } else { // not actually taken
        if (predictPC == fallThroughAddr) {
            correctTarg = true;
            if (isControlFlow) {
                cnt_correctPredTarg++;
            }
        }
    }
    if (correctTarg && correctDir && isControlFlow)
        cnt_correctPred++;
    // ------------------------------------------

    // ------------------------------------------
    if (isControlFlow) {
        // Update the state of the predictor:  (@ execute stage only)
        myBPU->UpdatePredictor(
                PC,               // address of instruction executing now
                brTaken,          // the actual direction
                targetPC,         // the next PC, **if taken**
                fallThroughAddr,  // return address for subroutine calls,
                //     DO NOT STORE IN BTB!
                isCall,           // is a subroutine call
                isReturn,         // is a return from subroutine
                correctDir,       // my direction prediction was correct
                correctTarg       // my target prediction was correct
        );
        
        //extra statistics
        //////////////////////////////////////////////////////////////////////////////
        //if (isCall){isCallCounter++;}
        //if (isReturn && correctTarg){isReturnCounter++;}
        /////////////////////////////////////////////////////////////////////////////
    }
    // ------------------------------------------
}

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

/*!
 * Insert call to the analysis routine before every branch instruction
 * of the trace.
 * This function is called every time a new trace is encountered.
 * @param[in]   trace    trace to be instrumented
 * @param[in]   v        value specified by the tool in the TRACE_AddInstrumentFunction
 *                       function call
 */
VOID Instruction(INS ins, VOID *v)
{
    if (INS_IsBranchOrCall(ins)) {   // Branch or call. Includes returns
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) ProcessBranch,
                       IARG_INST_PTR,                 // The instruction address
                       IARG_BRANCH_TARGET_ADDR,       // target address of the branch, or return address
                       IARG_BRANCH_TAKEN,             // taken branch (0 - not taken. BOOL)
                       IARG_UINT32,  INS_Size(ins),   // instr. size - used to calculare return address for subroutine calls
                       IARG_BOOL, INS_IsCall(ins),    // is this a subroutine call (BOOL)
                       IARG_BOOL, INS_IsRet(ins),     // is this a subroutine return (BOOL)
                       IARG_BOOL, INS_IsBranchOrCall(ins),
                       IARG_END);
    } else {   //  not a flow-control instruction
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) ProcessBranch,
                       IARG_INST_PTR,                 // The instruction address
                       IARG_ADDRINT, (ADDRINT) 0,     // target address of the branch, or return address
                       IARG_BOOL, false,              // taken branch (0 - not taken. BOOL)
                       IARG_UINT32,  INS_Size(ins),   // instr. size - used to calculare return address for subroutine calls
                       IARG_BOOL, INS_IsCall(ins),    // is this a subroutine call (BOOL)
                       IARG_BOOL, INS_IsRet(ins),     // is this a subroutine return (BOOL)
                       IARG_BOOL, INS_IsBranchOrCall(ins),
                       IARG_END);
    }
}


/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID *v)
{
    // outFile is already opened in main.
    *outFile <<  "===================================================" << endl;
    *outFile <<  "This application is instrumented by BTBsim PIN tool" << endl;

    *outFile << "Instructions: " << cnt_instr << endl;
    *outFile << "Branches: " << cnt_branches << endl;
    *outFile << " taken: " << cnt_branches_taken << "(" << cnt_branches_taken*100.0/cnt_branches << "%)" << endl;
    *outFile << " Predicted (direction & target): " << cnt_correctPred << "(" << cnt_correctPred*100.0 /cnt_branches << "%)" << endl;
    *outFile << " Predicted direction: " << cnt_correctPredDir << "(" << cnt_correctPredDir*100.0 /cnt_branches << "%)" << endl;
    *outFile << " Predicted target: " << cnt_correctPredTarg << "(" << cnt_correctPredTarg*100.0 /cnt_branches << "%)" << endl;
    
    // -------------------------------------------
    //  Output any extra counters/statistics here
    // -------------------------------------------
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //*outFile << "\n Calls: " << isCallCounter  << endl;
    //*outFile << " Correct TargetPredicted Returns: " << isReturnCounter << endl;
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


    //  Report any predictor internal counters
    std::string s = myBPU->ReportCounters();
    if (!s.empty())
        *outFile << s;

    *outFile <<  "===================================================" << endl;
    outFile->close();
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char * argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid 
    if (PIN_Init(argc, argv))
        return Usage();

    // Check and open output file for simulation results.
    // Write to a file since cout and cerr may be closed by the application
    std::string fileName = KnobOutputFile.Value();
    if (fileName.empty()) {
        cerr << "ERROR: must have an output file.";
        exit(-1);
    }
    outFile = new std::ofstream(fileName.c_str());

    myBPU = new BPU(); // Initialise Branch Prediction Unit

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}
/* ===================================================================== */
/* eof */
/* ===================================================================== */
