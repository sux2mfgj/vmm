#ifndef __VMX_H
#define __VMX_H

int vmx_run(void);
int vmx_init(void);
int vmx_deinit(void);

// VMX_BASIC
#define VMX_BASIC_TRUE_CTLS (1ULL << 55)

// VM_ENTRY
#define VM_ENTRY_IA32E_MODE 0x00000200

// VM_EXIT
#define VM_EXIT_HOST_ADDR_SPACE_SIZE    0x00000200
#define VM_EXIT_ASK_INTR_ON_EXIT        0x00008000

// CPU based
#define CPU_BASED_HLT_EXITING                   0x00000080
#define CPU_BASED_ACTIVATE_ESCONDARY_CONTROLS   0x80000000

enum guest_activity_state {
    GUEST_ACTIVITY_ACTIVE       = 0,
    GUEST_ACTIVITY_HLT          = 1,
    GUEST_ACTIVITY_SHUTDOWN     = 2,
    GUEST_ACTIVITY_WAIT_SIPI    = 3,
};

enum vmcs_field {
    GUEST_ES_SELECTOR           = 0x00000800,
    GUEST_CS_SELECTOR           = 0x00000802,
    GUEST_SS_SELECTOR           = 0x00000804,
    GUEST_DS_SELECTOR           = 0x00000806,
    GUEST_FS_SELECTOR           = 0x00000808,
    GUEST_GS_SELECTOR           = 0x0000080a,
    GUEST_LDTR_SELECTOR         = 0x0000080c,
    GUEST_TR_SELECTOR           = 0x0000080e,

    GUEST_ES_LIMIT              = 0x00004800,
    GUEST_CS_LIMIT              = 0x00004802,
    GUEST_SS_LIMIT              = 0x00004804,
    GUEST_DS_LIMIT              = 0x00004806,
    GUEST_FS_LIMIT              = 0x00004808,
    GUEST_GS_LIMIT              = 0x0000480a,
    GUEST_LDTR_LIMIT            = 0x0000480c,
    GUEST_TR_LIMIT              = 0x0000480e,

    GUEST_ES_AR_BYTES           = 0x00004814,
    GUEST_CS_AR_BYTES           = 0x00004816,
    GUEST_SS_AR_BYTES           = 0x00004818,
    GUEST_DS_AR_BYTES           = 0x0000481a,
    GUEST_FS_AR_BYTES           = 0x0000481c,
    GUEST_GS_AR_BYTES           = 0x0000481e,
    GUEST_LDTR_AR_BYTES         = 0x00004820,
    GUEST_TR_AR_BYTES           = 0x00004822,

    GUEST_INTERRUPTIBILITY_INFO = 0x00004824,
    GUEST_ACTIVITY_STATE        = 0x00004826,

    GUEST_IA32_DEBUGCTL         = 0x00002802,

    //GUEST_PENDING_DBG_EXCEPTIONS= 0x00006822,



    //
    PIN_BASED_VM_EXEC_CONTROL   = 0x00004000,
    CPU_BASED_VM_EXEC_CONTROL   = 0x00004002,
    VM_EXIT_CONTROLS            = 0x0000400c,
    VM_ENTRY_CONTROLS           = 0x00004012,
    SECONDARY_VM_EXEC_CONTROL   = 0x0000401e,
    VM_EXIT_MSR_STORE_COUNT     = 0x0000400e,
    VM_EXIT_MSR_STORE_ADDR      = 0x00002006,
    VM_EXIT_MSR_LOAD_COUNT      = 0x00004010,
    VM_EXIT_MSR_LOAD_ADDR       = 0x00002008,
    VM_ENTRY_MSR_LOAD_COUNT     = 0x00004014,
    VM_ENTRY_INTR_INFO_FIELD    = 0x00004016,

    PAGE_FAULT_ERROR_CODE_MASK  = 0x00004006,
    PAGE_FAULT_ERROR_CODE_MATCH = 0x00004008,
    CR3_TARGET_COUNT            = 0x0000400a,
    VMCS_LINK_POINTER           = 0x00002800,
};

#endif
