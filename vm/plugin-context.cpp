// vim: set sts=2 ts=8 sw=2 tw=99 et:
// 
// Copyright (C) 2006-2015 AlliedModders LLC
// 
// This file is part of SourcePawn. SourcePawn is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// You should have received a copy of the GNU General Public License along with
// SourcePawn. If not, see http://www.gnu.org/licenses/.
//
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <limits.h>
#include <sp_vm_api.h>
#include "plugin-context.h"
#include "watchdog_timer.h"
#include "environment.h"
#include "method-info.h"

using namespace sp;
using namespace SourcePawn;

#define CELLBOUNDMAX  (INT_MAX/sizeof(cell_t))
#define STACKMARGIN    ((cell_t)(16*sizeof(cell_t)))

static const size_t kMinHeapSize = 16384;

PluginContext::PluginContext(PluginRuntime *pRuntime)
 : m_pRuntime(pRuntime),
   memory_(nullptr),
   data_size_(m_pRuntime->data().length),
   mem_size_(m_pRuntime->image()->HeapSize()),
   m_pNullVec(nullptr),
   m_pNullString(nullptr)
{
  // Compute and align a minimum memory amount.
  if (mem_size_ < data_size_)
    mem_size_ = data_size_;
  mem_size_ = ke::Align(mem_size_, sizeof(cell_t));

  // Add a minimum heap size if needed.
  if (mem_size_ < data_size_ + kMinHeapSize)
    mem_size_ = data_size_ + kMinHeapSize;
  assert(ke::IsAligned(mem_size_, sizeof(cell_t)));

  hp_ = data_size_;
  sp_ = mem_size_ - sizeof(cell_t);
  stp_ = sp_;
  frm_ = sp_;

  tracker_.pBase = (ucell_t *)malloc(1024);
  tracker_.pCur = tracker_.pBase;
  tracker_.size = 1024 / sizeof(cell_t);
}

PluginContext::~PluginContext()
{
  free(tracker_.pBase);
  delete[] memory_;
}

bool
PluginContext::Initialize()
{
  memory_ = new uint8_t[mem_size_];
  if (!memory_)
    return false;
  memset(memory_ + data_size_, 0, mem_size_ - data_size_);
  memcpy(memory_, m_pRuntime->data().bytes, data_size_);

  /* Initialize the null references */
  uint32_t index;
  if (FindPubvarByName("NULL_VECTOR", &index) == SP_ERROR_NONE) {
    sp_pubvar_t *pubvar;
    GetPubvarByIndex(index, &pubvar);
    m_pNullVec = pubvar->offs;
  } else {
    m_pNullVec = NULL;
  }

  if (FindPubvarByName("NULL_STRING", &index) == SP_ERROR_NONE) {
    sp_pubvar_t *pubvar;
    GetPubvarByIndex(index, &pubvar);
    m_pNullString = pubvar->offs;
  } else {
    m_pNullString = NULL;
  }

  return true;
}

int
PluginContext::HeapAlloc(unsigned int cells, cell_t *local_addr, cell_t **phys_addr)
{
  cell_t *addr;
  ucell_t realmem;

#if 0
  if (cells > CELLBOUNDMAX)
  {
    return SP_ERROR_ARAM;
  }
#else
  assert(cells < CELLBOUNDMAX);
#endif

  realmem = cells * sizeof(cell_t);

  /**
   * Check if the space between the heap and stack is sufficient.
   */
  if ((cell_t)(sp_ - hp_ - realmem) < STACKMARGIN)
    return SP_ERROR_HEAPLOW;

  addr = (cell_t *)(memory_ + hp_);
  /* store size of allocation in cells */
  *addr = (cell_t)cells;
  addr++;
  hp_ += sizeof(cell_t);

  *local_addr = hp_;

  if (phys_addr)
    *phys_addr = addr;

  hp_ += realmem;

  return SP_ERROR_NONE;
}

int
PluginContext::HeapPop(cell_t local_addr)
{
  cell_t cellcount;
  cell_t *addr;

  /* check the bounds of this address */
  local_addr -= sizeof(cell_t);
  if (local_addr < (cell_t)data_size_ || local_addr >= sp_)
    return SP_ERROR_INVALID_ADDRESS;

  addr = (cell_t *)(memory_ + local_addr);
  cellcount = (*addr) * sizeof(cell_t);
  /* check if this memory count looks valid */
  if ((signed)(hp_ - cellcount - sizeof(cell_t)) != local_addr)
    return SP_ERROR_INVALID_ADDRESS;

  hp_ = local_addr;

  return SP_ERROR_NONE;
}


int
PluginContext::HeapRelease(cell_t local_addr)
{
  if (local_addr < (cell_t)data_size_)
    return SP_ERROR_INVALID_ADDRESS;

  hp_ = local_addr - sizeof(cell_t);

  return SP_ERROR_NONE;
}

int
PluginContext::FindNativeByName(const char *name, uint32_t *index)
{
  return m_pRuntime->FindNativeByName(name, index);
}

int
PluginContext::GetNativeByIndex(uint32_t index, sp_native_t **native)
{
  return m_pRuntime->GetNativeByIndex(index, native);
}

uint32_t
PluginContext::GetNativesNum()
{
  return m_pRuntime->GetNativesNum();
}

int
PluginContext::FindPublicByName(const char *name, uint32_t *index)
{
  return m_pRuntime->FindPublicByName(name, index);
}

int
PluginContext::GetPublicByIndex(uint32_t index, sp_public_t **pblic)
{
  return m_pRuntime->GetPublicByIndex(index, pblic);
}

uint32_t
PluginContext::GetPublicsNum()
{
  return m_pRuntime->GetPublicsNum();
}

int
PluginContext::GetPubvarByIndex(uint32_t index, sp_pubvar_t **pubvar)
{
  return m_pRuntime->GetPubvarByIndex(index, pubvar);
}

int
PluginContext::FindPubvarByName(const char *name, uint32_t *index)
{
  return m_pRuntime->FindPubvarByName(name, index);
}

int
PluginContext::GetPubvarAddrs(uint32_t index, cell_t *local_addr, cell_t **phys_addr)
{
  return m_pRuntime->GetPubvarAddrs(index, local_addr, phys_addr);
}

uint32_t
PluginContext::GetPubVarsNum()
{
  return m_pRuntime->GetPubVarsNum();
}

int
PluginContext::LocalToPhysAddr(cell_t local_addr, cell_t **phys_addr)
{
  if (((local_addr >= hp_) && (local_addr < sp_)) ||
      (local_addr < 0) || ((ucell_t)local_addr >= mem_size_))
  {
    return SP_ERROR_INVALID_ADDRESS;
  }

  if (phys_addr)
    *phys_addr = (cell_t *)(memory_ + local_addr);

  return SP_ERROR_NONE;
}

int
PluginContext::LocalToString(cell_t local_addr, char **addr)
{
  if (((local_addr >= hp_) && (local_addr < sp_)) ||
      (local_addr < 0) || ((ucell_t)local_addr >= mem_size_))
  {
    return SP_ERROR_INVALID_ADDRESS;
  }
  *addr = (char *)(memory_ + local_addr);

  return SP_ERROR_NONE;
}

int
PluginContext::StringToLocal(cell_t local_addr, size_t bytes, const char *source)
{
  char *dest;
  size_t len;

  if (((local_addr >= hp_) && (local_addr < sp_)) ||
      (local_addr < 0) || ((ucell_t)local_addr >= mem_size_))
  {
    return SP_ERROR_INVALID_ADDRESS;
  }

  if (bytes == 0)
    return SP_ERROR_NONE;

  len = strlen(source);
  dest = (char *)(memory_ + local_addr);

  if (len >= bytes)
    len = bytes - 1;

  memmove(dest, source, len);
  dest[len] = '\0';

  return SP_ERROR_NONE;
}

static inline int
__CheckValidChar(char *c)
{
  int count;
  int bytecount = 0;

  for (count=1; (*c & 0xC0) == 0x80; count++)
    c--;

  switch (*c & 0xF0)
  {
  case 0xC0:
  case 0xD0:
    {
      bytecount = 2;
      break;
    }
  case 0xE0:
    {
      bytecount = 3;
      break;
    }
  case 0xF0:
    {
      bytecount = 4;
      break;
    }
  }

  if (bytecount != count)
    return count;

  return 0;
}

int
PluginContext::StringToLocalUTF8(cell_t local_addr, size_t maxbytes, const char *source, size_t *wrtnbytes)
{
  char *dest;
  size_t len;
  bool needtocheck = false;

  if (((local_addr >= hp_) && (local_addr < sp_)) ||
      (local_addr < 0) ||
      ((ucell_t)local_addr >= mem_size_))
  {
    return SP_ERROR_INVALID_ADDRESS;
  }
  
  if (maxbytes == 0)
    return SP_ERROR_NONE;

  len = strlen(source);
  dest = (char *)(memory_ + local_addr);

  if ((size_t)len >= maxbytes) {
    len = maxbytes - 1;
    needtocheck = true;
  }

  memmove(dest, source, len);
  if ((dest[len-1] & 1<<7) && needtocheck)
    len -= __CheckValidChar(dest+len-1);
  dest[len] = '\0';

  if (wrtnbytes)
    *wrtnbytes = len;

  return SP_ERROR_NONE;
}

IPluginFunction *
PluginContext::GetFunctionById(funcid_t func_id)
{
  return m_pRuntime->GetFunctionById(func_id);
}

IPluginFunction *
PluginContext::GetFunctionByName(const char *public_name)
{
  return m_pRuntime->GetFunctionByName(public_name);
}

int
PluginContext::LocalToStringNULL(cell_t local_addr, char **addr)
{
  int err;
  if ((err = LocalToString(local_addr, addr)) != SP_ERROR_NONE)
    return err;

  if ((cell_t *)*addr == m_pNullString)
    *addr = NULL;

  return SP_ERROR_NONE;
}

cell_t *
PluginContext::GetNullRef(SP_NULL_TYPE type)
{
  if (type == SP_NULL_VECTOR)
    return m_pNullVec;

  return NULL;
}

bool
PluginContext::IsInExec()
{
  for (InvokeFrame *ivk = env_->top(); ivk; ivk = ivk->prev()) {
    if (ivk->cx() == this)
      return true;
  }
  return false;
}

bool
PluginContext::Invoke(funcid_t fnid, const cell_t *params, unsigned int num_params, cell_t *result)
{
  EnterProfileScope profileScope("SourcePawn", "EnterJIT");

  if (!env_->watchdog()->HandleInterrupt()) {
    ReportErrorNumber(SP_ERROR_TIMEOUT);
    return false;
  }

  assert((fnid & 1) != 0);

  unsigned public_id = fnid >> 1;
  ScriptedInvoker *cfun = m_pRuntime->GetPublicFunction(public_id);
  if (!cfun) {
    ReportErrorNumber(SP_ERROR_NOT_FOUND);
    return false;
  }

  if (m_pRuntime->IsPaused()) {
    ReportErrorNumber(SP_ERROR_NOT_RUNNABLE);
    return false;
  }

  if ((cell_t)(hp_ + 16*sizeof(cell_t)) > (cell_t)(sp_ - (sizeof(cell_t) * (num_params + 1)))) {
    ReportErrorNumber(SP_ERROR_STACKLOW);
    return false;
  }

  // Yuck. We have to do this for compatibility, otherwise something like
  // ForwardSys or any sort of multi-callback-fire code would die. Later,
  // we'll expose an Invoke() or something that doesn't do this.
  env_->clearPendingException();

  cell_t ignore_result;
  if (result == NULL)
    result = &ignore_result;

  /* We got this far.  It's time to start profiling. */
  EnterProfileScope scriptScope("SourcePawn", cfun->DebugName());

  /* See if we have to compile the callee. */
  RefPtr<MethodInfo> method = cfun->AcquireMethod();
  if (!method) {
    ReportErrorNumber(SP_ERROR_INVALID_ADDRESS);
    return false;
  }

  int err = method->Validate();
  if (err != SP_ERROR_NONE) {
    ReportErrorNumber(err);
    return false;
  }

  /* Save our previous state. */
  cell_t save_sp = sp_;
  cell_t save_hp = hp_;

  /* Push parameters */
  sp_ -= sizeof(cell_t) * (num_params + 1);
  cell_t *sp = (cell_t *)(memory_ + sp_);

  sp[0] = num_params;
  for (unsigned int i = 0; i < num_params; i++)
    sp[i + 1] = params[i];

  // Enter the execution engine.
  bool ok = env_->Invoke(this, method, result);

  if (ok) {
    // Verify that our state is still sane.
    if (sp_ != save_sp) {
      env_->ReportErrorFmt(
        SP_ERROR_STACKLEAK,
        "Stack leak detected: sp:%d should be %d!", 
        sp_, 
        save_sp);
      return false;
    }
    if (hp_ != save_hp) {
      env_->ReportErrorFmt(
        SP_ERROR_HEAPLEAK,
        "Heap leak detected: hp:%d should be %d!", 
        hp_, 
        save_hp);
      return false;
    }
  }

  sp_ = save_sp;
  hp_ = save_hp;
  return ok;
}

IPluginRuntime *
PluginContext::GetRuntime()
{
  return m_pRuntime;
}

cell_t *
PluginContext::GetLocalParams()
{
  return (cell_t *)(memory_ + frm_ + (2 * sizeof(cell_t)));
}

int
PluginContext::popTrackerAndSetHeap()
{
  assert(tracker_.pCur > tracker_.pBase);

  tracker_.pCur--;
  if (tracker_.pCur < tracker_.pBase)
    return SP_ERROR_TRACKER_BOUNDS;

  ucell_t amt = *tracker_.pCur;
  if (amt > (hp_ - data_size_))
    return SP_ERROR_HEAPMIN;

  hp_ -= amt;
  return SP_ERROR_NONE;
}

int
PluginContext::pushTracker(uint32_t amount)
{
  if ((size_t)(tracker_.pCur - tracker_.pBase) >= tracker_.size)
    return SP_ERROR_TRACKER_BOUNDS;

  if (tracker_.pCur + 1 - (tracker_.pBase + tracker_.size) == 0) {
    size_t disp = tracker_.size - 1;
    tracker_.size *= 2;
    tracker_.pBase = (ucell_t *)realloc(tracker_.pBase, tracker_.size * sizeof(cell_t));

    if (!tracker_.pBase)
      return SP_ERROR_TRACKER_BOUNDS;

    tracker_.pCur = tracker_.pBase + disp;
  }

  *tracker_.pCur++ = amount;
  return SP_ERROR_NONE;
}

struct array_creation_t
{
  const cell_t *dim_list;     /* Dimension sizes */
  cell_t dim_count;           /* Number of dimensions */
  cell_t *data_offs;          /* Current offset AFTER the indirection vectors (data) */
  cell_t *base;               /* array base */
};

static cell_t
GenerateInnerArrayIndirectionVectors(array_creation_t *ar, int dim, cell_t cur_offs)
{
  cell_t write_offs = cur_offs;
  cell_t *data_offs = ar->data_offs;

  cur_offs += ar->dim_list[dim];

  // Dimension n-x where x > 2 will have sub-vectors.  
  // Otherwise, we just need to reference the data section.
  if (ar->dim_count > 2 && dim < ar->dim_count - 2) {
    // For each index at this dimension, write offstes to our sub-vectors.
    // After we write one sub-vector, we generate its sub-vectors recursively.
    // At the end, we're given the next offset we can use.
    for (int i = 0; i < ar->dim_list[dim]; i++) {
      ar->base[write_offs] = (cur_offs - write_offs) * sizeof(cell_t);
      write_offs++;
      cur_offs = GenerateInnerArrayIndirectionVectors(ar, dim + 1, cur_offs);
    }
  } else {
    // In this section, there are no sub-vectors, we need to write offsets 
    // to the data.  This is separate so the data stays in one big chunk.
    // The data offset will increment by the size of the last dimension, 
    // because that is where the data is finally computed as. 
    for (int i = 0; i < ar->dim_list[dim]; i++) {
      ar->base[write_offs] = (*data_offs - write_offs) * sizeof(cell_t);
      write_offs++;
      *data_offs = *data_offs + ar->dim_list[dim + 1];
    }
  }

  return cur_offs;
}

static cell_t
calc_indirection(const array_creation_t *ar, cell_t dim)
{
  cell_t size = ar->dim_list[dim];
  if (dim < ar->dim_count - 2)
    size += ar->dim_list[dim] * calc_indirection(ar, dim + 1);
  return size;
}

static cell_t
GenerateArrayIndirectionVectors(cell_t *arraybase, cell_t dims[], cell_t _dimcount, bool autozero)
{
  array_creation_t ar;
  cell_t data_offs;

  /* Reverse the dimensions */
  cell_t dim_list[sDIMEN_MAX];
  int cur_dim = 0;
  for (int i = _dimcount - 1; i >= 0; i--)
    dim_list[cur_dim++] = dims[i];
  
  ar.base = arraybase;
  ar.dim_list = dim_list;
  ar.dim_count = _dimcount;
  ar.data_offs = &data_offs;

  data_offs = calc_indirection(&ar, 0);
  GenerateInnerArrayIndirectionVectors(&ar, 0, 0);
  return data_offs;
}

int
PluginContext::generateFullArray(uint32_t argc, cell_t *argv, int autozero)
{
  // Calculate how many cells are needed.
  if (argv[0] <= 0)
    return SP_ERROR_ARRAY_TOO_BIG;

  uint32_t cells = argv[0];

  for (uint32_t dim = 1; dim < argc; dim++) {
    cell_t dimsize = argv[dim];
    if (dimsize <= 0)
      return SP_ERROR_ARRAY_TOO_BIG;
    if (!ke::IsUint32MultiplySafe(cells, dimsize))
      return SP_ERROR_ARRAY_TOO_BIG;
    cells *= uint32_t(dimsize);
    if (!ke::IsUint32AddSafe(cells, dimsize))
      return SP_ERROR_ARRAY_TOO_BIG;
    cells += uint32_t(dimsize);
  }

  if (!ke::IsUint32MultiplySafe(cells, 4))
    return SP_ERROR_ARRAY_TOO_BIG;

  uint32_t bytes = cells * 4;
  if (!ke::IsUint32AddSafe(hp_, bytes))
    return SP_ERROR_ARRAY_TOO_BIG;

  uint32_t new_hp = hp_ + bytes;
  cell_t *dat_hp = reinterpret_cast<cell_t *>(memory_ + new_hp);

  // argv, coincidentally, is STK.
  if (dat_hp >= argv - STACK_MARGIN)
    return SP_ERROR_HEAPLOW;

  if (int err = pushTracker(bytes))
    return err;

  cell_t *base = reinterpret_cast<cell_t *>(memory_ + hp_);
  cell_t offs = GenerateArrayIndirectionVectors(base, argv, argc, !!autozero);
  assert(size_t(offs) == cells);
  (void)offs;

  argv[argc - 1] = hp_;
  hp_ = new_hp;
  return SP_ERROR_NONE;
}

int
PluginContext::generateArray(cell_t dims, cell_t *stk, bool autozero)
{
  if (dims == 1) {
    uint32_t size = *stk;
    if (size == 0 || !ke::IsUint32MultiplySafe(size, 4))
      return SP_ERROR_ARRAY_TOO_BIG;
    *stk = hp_;

    uint32_t bytes = size * 4;

    hp_ += bytes;
    if (uintptr_t(memory_ + hp_) >= uintptr_t(stk))
      return SP_ERROR_HEAPLOW;

    if (int err = pushTracker(bytes))
      return err;

    if (autozero)
      memset(memory_ + hp_, 0, bytes);

    return SP_ERROR_NONE;
  }

  if (int err = generateFullArray(dims, stk, autozero))
    return err;

  return SP_ERROR_NONE;
}

bool
PluginContext::pushAmxFrame()
{
  if (!pushStack(frm_))
    return false;
  if (!pushStack(0)) // unused cip
    return false;
  frm_ = sp_;
  return true;
}

bool
PluginContext::popAmxFrame()
{
  assert(sp_ == frm_);

  cell_t ignore;
  if (!popStack(&ignore))
    return false;
  if (!popStack(&frm_))
    return false;

  cell_t nargs;
  if (!popStack(&nargs))
    return false;

  if (nargs < 0 || cell_t(sp_ + nargs * sizeof(cell_t)) > stp_)
  {
    ReportErrorNumber(SP_ERROR_STACKMIN);
    return false;
  }

  sp_ += nargs * sizeof(cell_t);
  return true;
}

bool
PluginContext::pushStack(cell_t value)
{
  if (sp_ <= cell_t(hp_ + sizeof(cell_t))) {
    ReportErrorNumber(SP_ERROR_STACKLOW);
    return false;
  }
  sp_ -= sizeof(cell_t);

  *reinterpret_cast<cell_t*>(memory_ + sp_) = value;
  return true;
}

bool
PluginContext::popStack(cell_t* out)
{
  if (sp_ >= stp_) {
    ReportErrorNumber(SP_ERROR_STACKMIN);
    return false;
  }
  *out = *reinterpret_cast<cell_t*>(memory_ + sp_);

  sp_ += sizeof(cell_t);
  return true;
}

bool
PluginContext::getFrameValue(cell_t offset, cell_t* out)
{
  cell_t* addr = throwIfBadAddress(frm_ + offset);
  if (!addr)
    return false;

  *out = *addr;
  return true;
}

bool
PluginContext::setFrameValue(cell_t offset, cell_t value)
{
  cell_t* addr = throwIfBadAddress(frm_ + offset);
  if (!addr)
    return false;

  *addr = value;
  return true;
}

bool
PluginContext::getCellValue(cell_t address, cell_t* out)
{
  cell_t* ptr = throwIfBadAddress(address);
  if (!ptr)
    return false;

  *out = *ptr;
  return true;
}

bool
PluginContext::setCellValue(cell_t address, cell_t value)
{
  cell_t* ptr = throwIfBadAddress(address);
  if (!ptr)
    return false;

  *ptr = value;
  return true;
}

bool
PluginContext::heapAlloc(cell_t amount, cell_t* out)
{
  cell_t new_hp = hp_ + amount;

  if (amount < 0) {
    // Note: signed compare, in case new_hp is negative.
    if (new_hp < cell_t(data_size_)) {
      ReportErrorNumber(SP_ERROR_HEAPMIN);
      return false;
    }
  } else {
    if (new_hp + STACK_MARGIN > sp_) {
      ReportErrorNumber(SP_ERROR_HEAPLOW);
      return false;
    }
  }

  *out = hp_;
  hp_ = new_hp;
  return true;
}

cell_t*
PluginContext::acquireAddrRange(cell_t address, uint32_t bounds)
{
  cell_t* addr = throwIfBadAddress(address);
  if (!addr)
    return nullptr;
  if (bounds && !throwIfBadAddress(address + bounds - 1))
    return nullptr;
  return addr;
}

cell_t*
PluginContext::throwIfBadAddress(cell_t addr)
{
  if (addr < 0 ||
      (addr >= hp_ && addr < sp_) ||
      addr >= stp_)
  {
    ReportErrorNumber(SP_ERROR_INVALID_ADDRESS);
    return nullptr;
  }
  return reinterpret_cast<cell_t*>(memory_ + addr);
}

bool
PluginContext::addStack(cell_t amount)
{
  cell_t new_sp = sp_ + amount;

  if (amount < 0) {
    // Note: signed compare, in case new_sp is negative.
    if (new_sp < hp_ + STACK_MARGIN) {
      ReportErrorNumber(SP_ERROR_STACKLOW);
      return false;
    }
  } else {
    if (new_sp > stp_) {
      ReportErrorNumber(SP_ERROR_STACKMIN);
      return false;
    }
  }

  sp_ = new_sp;
  return true;
}
