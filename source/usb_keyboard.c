#include "usb_keyboard.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* A single boot keyboard, no report IDs, six-key rollover, LED output on EP0. */
static const uint8_t report_descriptor[] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,
    0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,
    0x75,0x08,0x81,0x03,0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,
    0x29,0x05,0x91,0x02,0x95,0x01,0x75,0x03,0x91,0x03,0x95,0x06,
    0x75,0x08,0x15,0x00,0x25,0x65,0x05,0x07,0x19,0x00,0x29,
    0x65,0x81,0x00,0xc0
};
static struct usb_hid_descriptor hid_descriptor = {
    .bLength=9,.bDescriptorType=USB_DT_HID,.bcdHID=0x0111,.bCountryCode=0,
    .bNumDescriptors=1,.bClassDescriptorType=USB_DT_REPORT,
    .wClassDescriptorLength=sizeof(report_descriptor)
};
_Static_assert(sizeof(struct usb_hid_descriptor)==9,"HID descriptor must be packed");
_Static_assert(sizeof(struct usb_control_setup)==8,"USB SETUP packet must be 8 bytes");
static KeyboardStatus status;
static UsbDsInterface *intf;
static UsbDsEndpoint *endpoint;
static bool service_open;
#ifdef _MSC_VER
#define DMA_ALIGN __declspec(align(4096))
#else
#define DMA_ALIGN __attribute__((aligned(0x1000)))
#endif
DMA_ALIGN static uint8_t transfer_buffer[0x1000];
DMA_ALIGN static uint8_t ctrl_in[0x1000];
DMA_ALIGN static uint8_t ctrl_out[0x1000];
static uint8_t last_report[8];
static uint32_t transfer_id, ctrl_in_id, ctrl_out_id;
static uint64_t transfer_started, last_send, control_started;
static bool ctrl_in_pending, ctrl_out_pending, led_data_pending;
static bool control_read, descriptor_request, descriptor_in_done, descriptor_out_done;
static bool status_out_after_in;
static uint32_t ctrl_in_expected, ctrl_out_expected;
static uint64_t configured_at, log_milliseconds, log_origin;
static bool log_origin_set;
static FILE *log_file;

void keyboard_log(const char *format, ...) {
    if (!log_file) return;
    fprintf(log_file,"[%8llu ms] ",(unsigned long long)log_milliseconds);
    va_list args; va_start(args,format); vfprintf(log_file,format,args); va_end(args);
    fputc('\n',log_file); fflush(log_file);
}

static Result failed(const char *stage, Result rc) {
    status.error=rc; snprintf(status.stage,sizeof(status.stage),"%s",stage);
    keyboard_log("ERROR %s: 0x%08lx module=%lu description=%lu",stage,(unsigned long)rc,
        (unsigned long)(rc&0x1ff),(unsigned long)((rc>>9)&0x1fff)); return rc;
}
#define STEP(name, call) do { rc=(call); if (R_FAILED(rc)) { failed(name,rc); goto fail; } } while (0)

Result keyboard_init(void) {
    memset(&status,0,sizeof(status)); memset(last_report,0,sizeof(last_report));
    ctrl_in_pending=ctrl_out_pending=led_data_pending=false;
    descriptor_request=descriptor_in_done=descriptor_out_done=false;
    control_read=status_out_after_in=false; configured_at=last_send=0;
    log_milliseconds=log_origin=0; log_origin_set=false;
    status.protocol=1; status.state=UsbState_Detached;
    log_file=fopen("sdmc:/switch/plume-usb/usb.log","w");
    keyboard_log("Plume USB 0.1.7 | HOS %u.%u.%u | applet=%d",
        HOSVER_MAJOR(hosversionGet()),HOSVER_MINOR(hosversionGet()),HOSVER_MICRO(hosversionGet()),appletGetAppletType());
    Result rc=0;
    if (hosversionBefore(5,0,0)) return failed("Horizon OS 5.0.0 required",MAKERESULT(Module_Libnx,LibnxError_IncompatSysVer));
    STEP("Open usb:ds",usbDsInitialize()); service_open=true;
    STEP("Clear USB descriptors",usbDsClearDeviceData());
    uint16_t langs[]={0x0409}; uint8_t language,manufacturer,product,serial;
    STEP("USB language string",usbDsAddUsbLanguageStringDescriptor(&language,langs,1));
    STEP("USB manufacturer string",usbDsAddUsbStringDescriptor(&manufacturer,"Plume Homebrew"));
    STEP("USB product string",usbDsAddUsbStringDescriptor(&product,"Plume USB Keyboard"));
    STEP("USB serial string",usbDsAddUsbStringDescriptor(&serial,"PLUME-TEST-001"));
    /* Experimental local identifier; not an impersonation of a Nintendo accessory. */
    struct usb_device_descriptor dev={
        .bLength=18,.bDescriptorType=USB_DT_DEVICE,.bcdUSB=0x0200,.bMaxPacketSize0=64,
        .idVendor=0x1209,.idProduct=0x0001,.bcdDevice=0x0107,
        .iManufacturer=manufacturer,.iProduct=product,.iSerialNumber=serial,.bNumConfigurations=1
    };
    STEP("Full Speed descriptor",usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full,&dev));
    STEP("High Speed descriptor",usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High,&dev));
    dev.bcdUSB=0x0300; dev.bMaxPacketSize0=9;
    STEP("Super Speed descriptor",usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super,&dev));
    const uint8_t bos[]={5,15,22,0,2, 7,16,2,2,0,0,0, 10,16,3,0,14,0,1,10,0xff,7};
    STEP("BOS descriptor",usbDsSetBinaryObjectStore(bos,sizeof(bos)));
    /* Interface zero is important for firmware with simple boot-keyboard parsers. */
    STEP("Keyboard interface 0",usbDsRegisterInterfaceEx(&intf,0));
    struct usb_interface_descriptor id={
        .bLength=9,.bDescriptorType=USB_DT_INTERFACE,.bInterfaceNumber=0,
        .bNumEndpoints=1,.bInterfaceClass=USB_CLASS_HID,.bInterfaceSubClass=1,.bInterfaceProtocol=1
    };
    struct usb_endpoint_descriptor ep={
        .bLength=7,.bDescriptorType=USB_DT_ENDPOINT,.bEndpointAddress=0x81,
        .bmAttributes=USB_TRANSFER_TYPE_INTERRUPT,.wMaxPacketSize=8,.bInterval=1
    };
    for (unsigned i=0;i<3;i++) {
        UsbDeviceSpeed speed=i==0?UsbDeviceSpeed_Full:i==1?UsbDeviceSpeed_High:UsbDeviceSpeed_Super;
        ep.bInterval=i==0?1:4; /* request 1ms polling at FS, HS and SS */
        STEP("HID interface",usbDsInterface_AppendConfigurationData(intf,speed,&id,sizeof(id)));
        STEP("HID descriptor",usbDsInterface_AppendConfigurationData(intf,speed,&hid_descriptor,sizeof(hid_descriptor)));
        STEP("Interrupt endpoint",usbDsInterface_AppendConfigurationData(intf,speed,&ep,7));
        if (speed==UsbDeviceSpeed_Super) {
            const uint8_t companion[]={6,48,0,0,8,0};
            STEP("Endpoint Super Speed",usbDsInterface_AppendConfigurationData(intf,speed,companion,sizeof(companion)));
        }
    }
    STEP("Register endpoint 81",usbDsInterface_RegisterEndpoint(intf,&endpoint,0x81));
    STEP("Enable interface",usbDsInterface_EnableInterface(intf));
    STEP("Enable USB keyboard",usbDsEnable());
    status.initialized=true; snprintf(status.stage,sizeof(status.stage),"USB initialization complete");
    keyboard_log("USB keyboard enabled | report descriptor=%u bytes",(unsigned)sizeof(report_descriptor));
    return 0;
fail:
    if (service_open) { usbDsExit(); service_open=false; }
    intf=NULL; endpoint=NULL; return rc;
}

static bool signalled(Event *event) {
    if (R_FAILED(eventWait(event,0))) return false;
    eventClear(event); return true;
}
static void control_stall(void) {
    Result rc=usbDsInterface_StallCtrl(intf);
    keyboard_log("Control STALL -> %08lx",(unsigned long)rc);
    ctrl_in_pending=ctrl_out_pending=led_data_pending=false;
    status_out_after_in=false;
    descriptor_request=false;
}
static void queue_status_out(uint64_t now) {
    Result rc=usbDsInterface_CtrlOutPostBufferAsync(intf,ctrl_out,0,&ctrl_out_id);
    if(R_FAILED(rc)) { failed("USB control OUT status",rc);control_stall();return; }
    ctrl_out_pending=true;ctrl_out_expected=0;control_started=now;
    keyboard_log("CTRL OUT status queued AFTER IN completion urb=%lu bytes=0",(unsigned long)ctrl_out_id);
}
static void reply(const void *data,size_t size,uint16_t requested,uint64_t now) {
    if(size>requested) size=requested;
    if(size) memcpy(ctrl_in,data,size);
    Result rc;
    /* usb:ds on hardware rejected IN when OUT status had already been queued.
       Submit IN data first; the completion handler advances to OUT status. */
    status_out_after_in=control_read;
    rc=usbDsInterface_CtrlInPostBufferAsync(intf,ctrl_in,size,&ctrl_in_id);
    if(R_FAILED(rc)) { failed("USB control IN response",rc); control_stall(); return; }
    ctrl_in_pending=true;ctrl_in_expected=(uint32_t)size;control_started=now;
    keyboard_log("CTRL IN %s queued urb=%lu bytes=%lu",control_read?"data":"status",(unsigned long)ctrl_in_id,(unsigned long)size);
}
static void handle_setup(uint64_t now) {
    struct usb_control_setup s={0};
    Result rc=usbDsInterface_GetSetupPacket(intf,&s,sizeof(s));
    if(R_FAILED(rc)) { failed("Read USB setup request",rc); return; }
    status.setup_count++; status.last_request_type=s.bmRequestType;
    status.last_request=s.bRequest; status.last_value=s.wValue;
    keyboard_log("SETUP type=%02x req=%02x value=%04x index=%04x length=%u",
        s.bmRequestType,s.bRequest,s.wValue,s.wIndex,s.wLength);
    ctrl_in_pending=ctrl_out_pending=led_data_pending=false;
    status_out_after_in=false;
    control_read=(s.bmRequestType&0x80)!=0;
    descriptor_request=false;
    uint8_t type=s.bmRequestType;
    if(s.wIndex!=0) { control_stall(); return; }
    if(type==0x81 && s.bRequest==0 && s.wValue==0) {
        const uint16_t flags=0;reply(&flags,2,s.wLength,now); /* GET_STATUS (interface) */
    } else if(type==0x81 && s.bRequest==10 && s.wValue==0) {
        const uint8_t alternate=0;reply(&alternate,1,s.wLength,now); /* GET_INTERFACE */
    } else if(type==0x01 && s.bRequest==11 && s.wValue==0 && s.wLength==0) {
        reply(NULL,0,0,now); /* SET_INTERFACE (only alternate zero) */
    } else if(type==0x81 && s.bRequest==6 && (s.wValue>>8)==USB_DT_REPORT && (s.wValue&255)==0) {
        status.report_requests++;
        descriptor_request=s.wLength>=sizeof(report_descriptor);
        if(descriptor_request) {
            descriptor_in_done=descriptor_out_done=false;
            status.descriptor_valid=false;
        }
        reply(report_descriptor,sizeof(report_descriptor),s.wLength,now);
    } else if(type==0x81 && s.bRequest==6 && (s.wValue>>8)==USB_DT_HID && (s.wValue&255)==0) {
        reply(&hid_descriptor,sizeof(hid_descriptor),s.wLength,now);
    } else if(type==0xa1 && s.bRequest==1 && (s.wValue&255)==0) { /* GET_REPORT */
        if((s.wValue>>8)==1) reply(last_report,8,s.wLength,now);
        else if((s.wValue>>8)==2) reply(&status.leds,1,s.wLength,now);
        else control_stall();
    } else if(type==0xa1 && s.bRequest==2) reply(&status.idle,1,s.wLength,now);
    else if(type==0xa1 && s.bRequest==3) reply(&status.protocol,1,s.wLength,now);
    else if(type==0x21 && s.bRequest==10 && s.wLength==0 && (s.wValue&255)==0) { /* SET_IDLE */
        status.idle=s.wValue>>8; reply(NULL,0,0,now);
    } else if(type==0x21 && s.bRequest==11 && s.wLength==0 && s.wValue<=1) {
        status.protocol=(uint8_t)s.wValue; reply(NULL,0,0,now);
    } else if(type==0x21 && s.bRequest==9 && s.wValue==0x0200 && s.wLength==1) {
        memset(ctrl_out,0,sizeof(ctrl_out));
        rc=usbDsInterface_CtrlOutPostBufferAsync(intf,ctrl_out,1,&ctrl_out_id);
        if(R_FAILED(rc)) { failed("Receive keyboard LEDs",rc); control_stall(); }
        else {
            ctrl_out_pending=led_data_pending=true;ctrl_out_expected=1;control_started=now;
            keyboard_log("CTRL OUT LED data queued urb=%lu bytes=1",(unsigned long)ctrl_out_id);
        }
    } else control_stall();
}

void keyboard_cancel_transfer(void) {
    if(endpoint && status.transfer_pending) usbDsEndpoint_Cancel(endpoint);
    status.transfer_pending=false;
    /* The buffer must not be reused until Cancel has returned. */
    if(endpoint) {
        UsbDsReportData reports;
        eventClear(&endpoint->CompletionEvent);
        usbDsEndpoint_GetReportData(endpoint,&reports);
    }
}

void keyboard_poll(uint64_t now) {
    if(!status.initialized) return;
    if(!log_origin_set) { log_origin=now;log_origin_set=true; }
    log_milliseconds=now-log_origin;
    UsbState state;
    Result rc=usbDsGetState(&state);
    if(R_FAILED(rc)) { failed("Read USB state",rc); status.ready=false; return; }
    if(state!=status.state) {
        keyboard_log("USB state %u -> %u",status.state,state);
        if(status.state==UsbState_Configured && state==UsbState_Default) status.bus_resets++;
        status.state=state;
        if(state==UsbState_Configured) configured_at=now;
        if(state!=UsbState_Configured) {
            keyboard_cancel_transfer();
            ctrl_in_pending=ctrl_out_pending=led_data_pending=false;
            status_out_after_in=false;
            descriptor_request=descriptor_in_done=descriptor_out_done=false;
            if(state!=UsbState_Suspended) status.descriptor_valid=false;
            memset(last_report,0,sizeof(last_report));
            if(state!=UsbState_Suspended) { status.leds=0;status.leds_known=false;status.protocol=1;status.idle=0; }
        }
    }
    /* Retire old completions before reading a new SETUP packet. A new SETUP
       legally supersedes an earlier control request. Never match it to an old URB. */
    if(signalled(&intf->CtrlInCompletionEvent)) {
        UsbDsReportData reports;
        rc=usbDsInterface_GetCtrlInReportData(intf,&reports);
        if(R_SUCCEEDED(rc)) {
            bool found=false;
            for(uint32_t i=0;i<reports.report_count && i<8;i++) {
                const UsbDsReportEntry *r=&reports.report[i];
                keyboard_log("CTRL IN done urb=%lu requested=%lu actual=%lu status=%lu",
                    (unsigned long)r->id,(unsigned long)r->requestedSize,(unsigned long)r->transferredSize,(unsigned long)r->urb_status);
                if(ctrl_in_pending && r->id==ctrl_in_id) found=true;
            }
            if(found) {
                uint32_t actual=0;rc=usbDsParseReportData(&reports,ctrl_in_id,NULL,&actual);
                ctrl_in_pending=false;
                if(R_SUCCEEDED(rc) && actual==ctrl_in_expected) {
                    status.control_in_completed++;
                    if(descriptor_request) descriptor_in_done=true;
                    if(status_out_after_in) {
                        status_out_after_in=false;
                        queue_status_out(now);
                    }
                } else { failed("USB control IN completion",R_FAILED(rc)?rc:MAKERESULT(Module_Libnx,LibnxError_BadUsbCommsWrite));control_stall(); }
            }
        } else failed("Read USB control IN result",rc);
    }
    if(signalled(&intf->CtrlOutCompletionEvent)) {
        UsbDsReportData reports; uint32_t transferred=0;
        rc=usbDsInterface_GetCtrlOutReportData(intf,&reports);
        if(R_SUCCEEDED(rc)) {
            bool found=false;
            for(uint32_t i=0;i<reports.report_count && i<8;i++) {
                const UsbDsReportEntry *r=&reports.report[i];
                keyboard_log("CTRL OUT done urb=%lu requested=%lu actual=%lu status=%lu",
                    (unsigned long)r->id,(unsigned long)r->requestedSize,(unsigned long)r->transferredSize,(unsigned long)r->urb_status);
                if(ctrl_out_pending && r->id==ctrl_out_id) found=true;
            }
            if(found) {
                rc=usbDsParseReportData(&reports,ctrl_out_id,NULL,&transferred);
                ctrl_out_pending=false;
                if(R_SUCCEEDED(rc) && transferred==ctrl_out_expected) {
                    status.control_out_completed++;
                    if(led_data_pending) {
                        led_data_pending=false;armDCacheFlush(ctrl_out,sizeof(ctrl_out));status.leds=ctrl_out[0];status.leds_known=true;
                        keyboard_log("LED report=%02x",status.leds);reply(NULL,0,0,now);
                    } else if(descriptor_request) descriptor_out_done=true;
                } else { failed("USB control OUT completion",R_FAILED(rc)?rc:MAKERESULT(Module_Libnx,LibnxError_BadUsbCommsWrite));control_stall(); }
            }
        } else failed("Read USB control OUT result",rc);
    }
    if(descriptor_request && descriptor_in_done && descriptor_out_done && !status.descriptor_valid) {
        status.descriptor_valid=true;keyboard_log("HID report descriptor transfer fully acknowledged");
    }
    if(signalled(&intf->SetupEvent)) handle_setup(now);
    if((ctrl_in_pending || ctrl_out_pending) && now-control_started>1500) {
        keyboard_log("Control transfer timeout IN=%u OUT=%u",ctrl_in_pending,ctrl_out_pending); control_stall();
    }
    bool ready=state==UsbState_Configured && status.descriptor_valid && now-configured_at>=300;
    if(ready!=status.ready) keyboard_log("HID ready=%u descriptor_valid=%u resets=%lu",ready,status.descriptor_valid,(unsigned long)status.bus_resets);
    status.ready=ready;
    if(signalled(&endpoint->CompletionEvent)) {
        UsbDsReportData reports; uint32_t transferred=0;
        rc=usbDsEndpoint_GetReportData(endpoint,&reports);
        if(R_SUCCEEDED(rc) && status.transfer_pending) rc=usbDsParseReportData(&reports,transfer_id,NULL,&transferred);
        if(status.transfer_pending) {
            status.transfer_pending=false;
            if(R_FAILED(rc) || transferred!=8) {
                status.reports_failed++; failed("Keyboard report completion",R_FAILED(rc)?rc:MAKERESULT(Module_Libnx,LibnxError_BadUsbCommsWrite));
            } else { status.reports_completed++; memcpy(last_report,transfer_buffer,8); }
        }
    }
    if(status.transfer_pending && now-transfer_started>2500) {
        keyboard_cancel_transfer(); status.reports_failed++;
        failed("Keyboard report timed out",KERNELRESULT(TimedOut));
    }
}

Result keyboard_send(uint8_t modifier,uint8_t key,uint64_t now) {
    if(!status.ready || status.transfer_pending) return KERNELRESULT(InvalidState);
    memset(transfer_buffer,0,8); transfer_buffer[0]=modifier; transfer_buffer[2]=key;
    Result rc=usbDsEndpoint_PostBufferAsync(endpoint,transfer_buffer,8,&transfer_id);
    if(R_FAILED(rc)) { status.reports_failed++; return failed("Send keyboard report",rc); }
    transfer_started=last_send=now; status.transfer_pending=true; return 0;
}
bool keyboard_idle_due(uint64_t now) {
    return status.ready && !status.transfer_pending && status.idle && now-last_send>=(uint64_t)status.idle*4;
}
const KeyboardStatus *keyboard_status(void) { return &status; }
void keyboard_exit(void) {
    keyboard_log("EXIT completed=%lu failed=%lu ctrl_in=%lu ctrl_out=%lu resets=%lu",
        (unsigned long)status.reports_completed,(unsigned long)status.reports_failed,
        (unsigned long)status.control_in_completed,(unsigned long)status.control_out_completed,(unsigned long)status.bus_resets);
    if(service_open) { keyboard_cancel_transfer(); usbDsExit(); }
    service_open=false; intf=NULL; endpoint=NULL; status.initialized=false;
    ctrl_in_pending=ctrl_out_pending=led_data_pending=false;
    status_out_after_in=false;
    if(log_file) fclose(log_file);
    log_file=NULL;
}
