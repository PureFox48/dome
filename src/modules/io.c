typedef struct {
  bool ready;
  size_t length;
  char* data;
} DBUFFER;

typedef struct {
  bool complete;
  bool error;
  WrenVM* vm;
  WrenHandle* bufferHandle;
} ASYNCOP;


internal void
ASYNCOP_allocate(WrenVM* vm) {
  // Get a handle to the Stat class. We'll hang on to this so we don't have to
  // look it up by name every time.
  wrenEnsureSlots(vm, 2);
  wrenSetSlotHandle(vm, 1, bufferClass);
  DBUFFER* buffer = (DBUFFER*)wrenSetSlotNewForeign(vm, 1, 1, sizeof(DBUFFER));
  buffer->data = NULL;
  buffer->length = 0;
  buffer->ready = false;

  ASYNCOP* op = (ASYNCOP*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(ASYNCOP));
  op->vm = vm;

  // This is a handle to our specific buffer, not the DataBuffer class
  op->bufferHandle = wrenGetSlotHandle(vm, 1);
  op->complete = false;
  op->error = false;
}

internal void
ASYNCOP_finalize(void* data) {
  ASYNCOP* op = data;
  wrenReleaseHandle(op->vm, op->bufferHandle);
}

internal void
ASYNCOP_getComplete(WrenVM* vm) {
  ASYNCOP* op = (ASYNCOP*)wrenGetSlotForeign(vm, 0);
  wrenEnsureSlots(vm, 1);
  wrenSetSlotBool(vm, 0, op->complete);
}

internal void
ASYNCOP_getResult(WrenVM* vm) {
  ASYNCOP* op = (ASYNCOP*)wrenGetSlotForeign(vm, 0);
  wrenEnsureSlots(vm, 1);
  wrenSetSlotHandle(vm, 0, op->bufferHandle);
}


internal void
DBUFFER_allocate(WrenVM* vm) {
  wrenEnsureSlots(vm, 1);
  DBUFFER* buffer = (DBUFFER*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(DBUFFER));
  buffer->data = NULL;
  buffer->length = 0;
  buffer->ready = false;
}

internal void
DBUFFER_finalize(void* data) {
  DBUFFER* buffer = (DBUFFER*) data;
  if (buffer->ready && buffer->data != NULL) {
    free(buffer->data);
  }
}

internal void
DBUFFER_getLength(WrenVM* vm) {
  DBUFFER* buffer = wrenGetSlotForeign(vm, 0);
  wrenEnsureSlots(vm, 1);
  wrenSetSlotDouble(vm, 0, buffer->length);
}

internal void
DBUFFER_getReady(WrenVM* vm) {
  DBUFFER* buffer = wrenGetSlotForeign(vm, 0);
  wrenEnsureSlots(vm, 1);
  wrenSetSlotBool(vm, 0, buffer->ready);
}

internal void
DBUFFER_getData(WrenVM* vm) {
  DBUFFER* buffer = wrenGetSlotForeign(vm, 0);
  wrenEnsureSlots(vm, 1);
  wrenSetSlotBytes(vm, 0, buffer->data, buffer->length);
}

typedef struct {
  WrenVM* vm;
  WrenHandle* opHandle;
  WrenHandle* bufferHandle;
  char name[256];
  size_t length;
  char* buffer;
} TASK_DATA;

internal void
FILESYSTEM_load(WrenVM* vm) {
  // Thread: main
  INIT_TO_ZERO(ABC_TASK, task);
  TASK_DATA* taskData = malloc(sizeof(TASK_DATA));

  const char* path = wrenGetSlotString(vm, 1);
  strncpy(taskData->name, path, 255);
  taskData->name[255] = '\0';

  // TODO We probably need to hold a handle to the op
  taskData->vm = vm;
  taskData->opHandle = wrenGetSlotHandle(vm, 2);
  ASYNCOP* op = wrenGetSlotForeign(vm, 2);
  taskData->bufferHandle = op->bufferHandle;
  taskData->buffer = NULL;
  taskData->length = 0;

  ENGINE* engine = (ENGINE*)wrenGetUserData(vm);
  task.type = TASK_LOAD_FILE;
  task.data = taskData;
  ABC_FIFO_pushTask(&engine->fifo, task);
}

internal void
FILESYSTEM_loadEventHandler(void* data) {
  TASK_DATA* task = data;

  // Thread: Async
  ENGINE* engine = (ENGINE*)wrenGetUserData(task->vm);
  task->buffer = ENGINE_readFile(engine, task->name, &task->length);

  SDL_Event event;
  SDL_memset(&event, 0, sizeof(event));
  event.type = ENGINE_EVENT_TYPE;
  event.user.code = EVENT_LOAD_FILE;
  event.user.data1 = task;
  // TODO: Use this to handle errors in future?
  event.user.data2 = NULL;
  SDL_PushEvent(&event);
}

internal void
FILESYSTEM_loadSync(WrenVM* vm) {
  // TODO: We should return a DataBuffer object rather than a string
  const char* path = wrenGetSlotString(vm, 1);
  ENGINE* engine = (ENGINE*)wrenGetUserData(vm);

  size_t length;
  char* data = ENGINE_readFile(engine, path, &length);
  if (data == NULL) {
    size_t len = 22 + strlen(path);
    char message[len];
    snprintf(message, len, "Could not find file: %s", path);
    VM_ABORT(vm, message);
    return;
  }
  wrenEnsureSlots(vm, 1);
  wrenSetSlotBytes(vm, 0, data, length);
  free(data);
}

internal void
FILESYSTEM_loadEventComplete(SDL_Event* event) {
  // Thread: Main
  TASK_DATA* task = event->user.data1;
  WrenVM* vm = task->vm;
  wrenEnsureSlots(vm, 4);

  wrenSetSlotHandle(vm, 1, task->opHandle);
  ASYNCOP* op = (ASYNCOP*)wrenGetSlotForeign(vm, 1);
  wrenSetSlotHandle(vm, 2, task->bufferHandle);
  DBUFFER* buffer = (DBUFFER*)wrenGetSlotForeign(vm, 2);

  buffer->data = task->buffer;
  buffer->length = task->length;
  buffer->ready = true;

  op->complete = true;

  // Free resources and handles
  wrenReleaseHandle(vm, task->opHandle);
  free(task);
}
