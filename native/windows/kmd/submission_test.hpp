// Actual preallocation/capture code with host allocator shims. These tests do
// not establish IRQL correctness or acceptance by the Windows GPU scheduler.
#pragma once

int submission_test(Adapter *adapter) {
    using namespace nr::kmd;
    auto &q = adapter->scheduler;
    auto &pool = adapter->submissions;
    const auto initial = outstanding;
    Ticket ticket{99, 99};
    DXGKARG_SUBMITCOMMAND args{};
    NR_CHECK(pool.capture(args, ticket) == STATUS_DEVICE_NOT_READY);
    NR_CHECK(pool.prepare(q, 0) == STATUS_INVALID_PARAMETER);
    NR_CHECK(pool.prepare(q, MAXULONG) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(!pool.ready() && outstanding == initial);
    const long failures[] = {0, 1};
    for (const long fail : failures) {
        fail_after = fail;
        NR_CHECK(pool.prepare(q, 2) == STATUS_INSUFFICIENT_RESOURCES);
        NR_CHECK(!pool.ready() && outstanding == initial && q.limit() == Scheduler::capacity);
    }
    fail_after = -1;
    NR_CHECK(pool.prepare(q, 2) == STATUS_SUCCESS);
    NR_CHECK(pool.capacity() == 3 && q.limit() == 3 && outstanding == initial + 2);
    NR_CHECK(pool.prepare(q, 2) == STATUS_INVALID_DEVICE_STATE);
    NR_CHECK(registration.DxgkDdiRemoveDevice(adapter) == STATUS_DEVICE_BUSY);
    NR_CHECK(q.start() == QueueResult::ok);
    NR_CHECK(pool.release() == STATUS_DEVICE_BUSY);
    NR_CHECK(adapter->segment.prepare(PAGE_SIZE) == STATUS_SUCCESS);

    // Private records can describe a subset of a larger paging buffer. They
    // must be snapshotted: changing the input after capture cannot change work.
    PagingCommand records[3]{};
    records[1].operation = DXGK_OPERATION_FILL;
    records[1].bytes = 16;
    records[1].pattern = 0x04030201;
    records[1].destination.offset = 12;
    records[2].operation = DXGK_OPERATION_DISCARD_CONTENT;
    args.Flags.Paging = 1;
    args.SubmissionFenceId = 0xffffffff;
    args.DmaBufferSize = 3 * sizeof(PagingMarker);
    args.DmaBufferSubmissionStartOffset = sizeof(PagingMarker);
    args.DmaBufferSubmissionEndOffset = args.DmaBufferSize;
    args.pDmaBufferPrivateData = records;
    args.DmaBufferPrivateDataSize = sizeof(records);
    args.DmaBufferPrivateDataSubmissionStartOffset = sizeof(PagingCommand);
    args.DmaBufferPrivateDataSubmissionEndOffset = sizeof(records);
    const auto valid = args;
    const auto valid_record = records[1];
    for (unsigned mutation = 0; mutation < 18; ++mutation) {
        args = valid;
        records[1] = valid_record;
        switch (mutation) {
        case 0: args.Flags.Paging = 0; break;
        case 1: args.Flags.ContextSwitch = 1; break;
        case 2: args.Flags.Flip = 1; break;
        case 3: args.NodeOrdinal = 1; break;
        case 4: args.EngineOrdinal = 1; break;
        case 5: args.DmaBufferSubmissionStartOffset = MAXULONG; break;
        case 6: args.DmaBufferSize--; break;
        case 7: args.DmaBufferPrivateDataSubmissionStartOffset = MAXULONG; break;
        case 8: args.DmaBufferPrivateDataSize--; break;
        case 9: args.pDmaBufferPrivateData = nullptr; break;
        case 10: args.DmaBufferSize = PAGE_SIZE + 1; break;
        case 11: args.DmaBufferSubmissionStartOffset++; break;
        case 12: args.DmaBufferPrivateDataSubmissionStartOffset++; break;
        case 13: args.DmaBufferPrivateDataSubmissionEndOffset -= sizeof(PagingCommand); break;
        case 14: records[1].magic = 0; break;
        case 15: records[1].reserved = 1; break;
        case 16: records[1].bytes = Segment::bytes + 1; break;
        case 17: records[1].operation = DXGK_OPERATION_MAP_APERTURE_SEGMENT; break;
        }
        NR_CHECK(!NT_SUCCESS(pool.capture(args, ticket)));
        NR_CHECK(!q.pending() && !q.active() && ticket.serial == 99 && ticket.epoch == 99);
    }
    args = valid;
    records[1] = valid_record;
    fail_after = 0; // Capture must not allocate after startup.
    NR_CHECK(pool.capture(args, ticket) == STATUS_SUCCESS);
    records[1].pattern = 0;
    const auto packet = pool.packet(ticket);
    NR_CHECK(packet && packet->commands == 2 && packet->paging[0].pattern == 0x04030201);
    NR_CHECK(!pool.packet({ticket.serial, ticket.epoch + 1}));
    NR_CHECK(q.completed() != args.SubmissionFenceId);
    Work work{};
    NR_CHECK(q.take(work) == QueueResult::ok && work.ticket.serial == ticket.serial);
    NR_CHECK(work.fence == 0xffffffff && work.bytes == 2 * sizeof(PagingMarker));
    const auto target = static_cast<const UCHAR *>(adapter->segment.range(12, 16));
    for (UINT i = 0; i < 16; ++i) NR_CHECK(target[i] == 0);
    for (UINT i = 0; i < packet->commands; ++i)
        NR_CHECK(Paging::execute(adapter->segment, packet->paging[i]) == STATUS_SUCCESS);
    for (UINT i = 0; i < 16; ++i) NR_CHECK(target[i] == (i % 4) + 1);
    NR_CHECK(q.complete(work.ticket) == QueueResult::ok && q.completed() == 0xffffffff);

    // Empty context switches must ignore invalid private fields when size=0.
    args = {};
    args.Flags.ContextSwitch = 1;
    args.pDmaBufferPrivateData = reinterpret_cast<void *>(MAXULONG_PTR);
    args.DmaBufferPrivateDataSubmissionStartOffset = MAXULONG;
    args.DmaBufferPrivateDataSubmissionEndOffset = 7;
    NR_CHECK(pool.capture(args, ticket) == STATUS_SUCCESS);
    NR_CHECK(pool.packet(ticket)->commands == 0);
    NR_CHECK(q.take(work) == QueueResult::ok && work.fence == 0);
    for (UINT i = 1; i < pool.capacity(); ++i) {
        args.SubmissionFenceId = i;
        NR_CHECK(pool.capture(args, ticket) == STATUS_SUCCESS);
    }
    NR_CHECK(pool.capture(args, ticket) == STATUS_INSUFFICIENT_RESOURCES);
    NR_CHECK(q.complete(work.ticket) == QueueResult::ok && q.completed() == 0);
    // Recycle ring storage many times, checking retained packet identity.
    for (UINT i = 0; i < 1000; ++i) {
        args.SubmissionFenceId = i;
        NR_CHECK(pool.capture(args, ticket) == STATUS_SUCCESS);
        NR_CHECK(q.take(work) == QueueResult::ok);
        NR_CHECK(pool.packet(work.ticket) && pool.packet(work.ticket)->commands == 0);
        NR_CHECK(q.complete(work.ticket) == QueueResult::ok);
    }
    NR_CHECK(q.take(work) == QueueResult::ok);
    NR_CHECK(q.stop() == QueueResult::ok);
    NR_CHECK(pool.release() == STATUS_DEVICE_BUSY);
    NR_CHECK(pool.packet(work.ticket) && outstanding == initial + 2);
    NR_CHECK(q.complete(work.ticket) == QueueResult::stale);
    NR_CHECK(q.drain(work.ticket) == QueueResult::ok);
    NR_CHECK(pool.release() == STATUS_SUCCESS);
    adapter->segment.release();
    NR_CHECK(!pool.ready() && !pool.packet(work.ticket) && outstanding == initial);
    NR_CHECK(q.limit() == Scheduler::capacity);
    NR_CHECK(pool.release() == STATUS_SUCCESS);
    fail_after = -1;
    return 0;
}
