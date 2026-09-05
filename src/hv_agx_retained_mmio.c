#include "hv_agx_retained_mmio.h"
#include <string.h>
unsigned char hv_agx_retained_mmio(struct hv_agx_retained_mmio *s,
    unsigned long long offset, unsigned long long *value, unsigned char write,
    unsigned width, hv_agx_retained_execute execute, void *context) {
  unsigned bytes;
  if (!s || !value || width > 3) return 0;
  bytes = 1u << width;
  if (offset & (bytes-1)) return 0;
  if (write && offset == AGX_RR_DOORBELL) {
    AGX_RR_REQUEST q = s->Request;
    AGX_RR_RESPONSE r = {0};
    if (width != 2 || *value != 1) return 0;
    r.Receipt = q.Sequence;
    r.Status = AGX_RR_STATUS_REQUEST;
    if (q.Version == 1 && q.Bytes == sizeof(q) && !q.Reserved &&
        q.Sequence && q.Sequence > s->LastSequence && execute &&
        q.Command >= AGX_RR_PREPARE && q.Command <= AGX_RR_CLOSE) {
      s->LastSequence = q.Sequence;
      execute(context, &q, &r);
    }
    s->Response = r;
    return 1;
  }
  if (offset <= sizeof(s->Request)-bytes) {
    if (write) memcpy((unsigned char *)&s->Request+offset,value,bytes);
    else { *value=0; memcpy(value,(unsigned char *)&s->Request+offset,bytes); }
    return 1;
  }
  if (!write && offset >= AGX_RR_RESPONSE_OFFSET &&
      offset-AGX_RR_RESPONSE_OFFSET <= sizeof(s->Response)-bytes) {
    *value=0;
    memcpy(value,(unsigned char *)&s->Response+offset-AGX_RR_RESPONSE_OFFSET,bytes);
    return 1;
  }
  return 0;
}
