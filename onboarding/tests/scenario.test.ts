import { describe, expect, it } from 'vitest';
import {
  CHAPTERS,
  EXPERIMENTS,
  experimentResult,
  FrameDecoderModel,
  ScenarioAuthorityModel,
  SessionLifecycleModel,
} from '../src/scenario';

describe('Inside Socket Chat curriculum', () => {
  it('publishes the agreed 13 chapter sequence and exact runtime', () => {
    expect(CHAPTERS.map((chapter) => chapter.duration)).toEqual([
      45, 60, 75, 90, 70, 80, 70, 80, 80, 80, 55, 100, 60,
    ]);
    expect(CHAPTERS.reduce((total, chapter) => total + chapter.duration, 0)).toBe(945);
    expect(CHAPTERS.map((chapter) => chapter.stage)).toEqual([
      'overview',
      'overview',
      'client',
      'framing',
      'tls',
      'session',
      'identity',
      'routing',
      'groups',
      'storage',
      'heartbeat',
      'recovery',
      'overview',
    ]);
  });

  it('keeps the private-room chapter on the agreed nine-beat timeline', () => {
    const chapter = CHAPTERS.find((item) => item.id === 'private-rooms');
    expect(chapter?.beats.map((beat) => beat.start)).toEqual([0, 8, 18, 28, 40, 53, 65, 72]);
    expect(chapter?.beats.at(-1)?.end).toBe(80);
    expect(chapter?.beats.map((beat) => beat.view)).toEqual([
      'wide',
      'detail',
      'detail',
      'cutaway',
      'cutaway',
      'wide',
      'wide',
      'wide',
    ]);
  });

  it('authors current group membership explicitly for each group beat', () => {
    const chapter = CHAPTERS.find((item) => item.id === 'groups');
    expect(chapter?.beats.map((beat) => beat.members)).toEqual([
      ['Alice'],
      ['Alice'],
      ['Alice'],
      ['Alice', 'Bob'],
      ['Alice', 'Bob'],
      ['Alice', 'Bob'],
    ]);
    expect(chapter?.beats.map((beat) => beat.start)).toEqual([0, 13, 27, 40, 53, 67]);
    expect(chapter?.beats.at(-1)?.end).toBe(80);
    expect(chapter?.beats[4]).toMatchObject({
      title: 'Member delivery',
      phase: 'deliver',
      view: 'wide',
      members: ['Alice', 'Bob'],
    });
    expect(chapter?.beats[5]).toMatchObject({ focus: 'group', phase: 'deny', view: 'cutaway' });
  });

  it('exposes all ten deterministic experiment outcomes', () => {
    expect(EXPERIMENTS.map((item) => item.id)).toEqual([
      'split-header',
      'coalesced',
      'oversized',
      'forged-sender',
      'nonmember',
      'slow-receiver',
      'cancel-write',
      'lost-ack',
      'reclaim-name',
      'partial-idle',
    ]);
    expect(experimentResult('forged-sender', 2)).toMatchObject({
      status: 'rejected',
      deliveries: [],
      committed: false,
    });
    expect(experimentResult('forged-sender', 1).explanation).toMatch(/denies|reject/i);
    expect(experimentResult('nonmember', 2)).toMatchObject({
      status: 'rejected',
      deliveries: [],
    });
    expect(experimentResult('lost-ack', 3)).toMatchObject({
      status: 'recovered',
      committed: true,
      deliveries: ['bob'],
      sequence: [7],
    });
    expect(experimentResult('partial-idle', 2)).toMatchObject({
      status: 'closed',
      receivedBytes: 2,
      emitted: 0,
    });
    expect(experimentResult('coalesced', 0)).toMatchObject({ receivedBytes: 48, emitted: 2 });
  });

  it('decodes split and coalesced frames in stream order and rejects invalid framing', () => {
    const decoder = new FrameDecoderModel();
    expect(decoder.feed([0, 0])).toEqual([]);
    expect(decoder.pendingHeaderBytes).toBe(2);
    expect(decoder.feed([0, 2, 65, 66])).toEqual([new Uint8Array([65, 66])]);
    expect(decoder.finish()).toBeUndefined();

    const coalesced = new FrameDecoderModel();
    expect(coalesced.feed([0, 0, 0, 1, 90, 0, 0, 0, 2, 88, 89])).toEqual([
      new Uint8Array([90]),
      new Uint8Array([88, 89]),
    ]);
    expect(coalesced.finish()).toBeUndefined();
    expect(() => new FrameDecoderModel().feed([0, 0, 0, 0])).toThrow(/length/i);
    expect(() => new FrameDecoderModel().feed([0, 16, 0, 1])).toThrow(/length/i);
    const truncated = new FrameDecoderModel();
    truncated.feed([0, 0]);
    expect(() => truncated.finish()).toThrow(/truncated/i);
  });

  it('models commit rollback, sender-scoped retry deduplication, and key conflicts', () => {
    const authority = new ScenarioAuthorityModel();
    const alice = authority.freshLogin('Alice');
    const bob = authority.freshLogin('Bob');
    const failed = authority.sendDirect(alice, bob, 'retry-key', 'hello', { failCommit: true });
    expect(failed.status).toBe('storage-failed');
    expect(authority.roomSequence(alice, bob)).toBe(0);
    const accepted = authority.sendDirect(alice, bob, 'retry-key', 'hello');
    expect(accepted).toMatchObject({ status: 'accepted', sequence: 1, duplicate: false });
    const retry = authority.sendDirect(alice, bob, 'retry-key', 'hello');
    expect(retry).toMatchObject({ status: 'accepted', sequence: 1, duplicate: true, fanout: 0 });
    expect(authority.sendDirect(alice, bob, 'retry-key', 'different text').status).toBe(
      'key-conflict',
    );
    expect(authority.roomSequence(alice, bob)).toBe(1);
  });

  it('rejects any nonempty claimed sender field, including the actual name', () => {
    const authority = new ScenarioAuthorityModel();
    const alice = authority.freshLogin('Alice');
    const bob = authority.freshLogin('Bob');
    expect(
      authority.sendDirect(alice, bob, 'forged', 'hello', { claimedSender: 'Alice' }),
    ).toMatchObject({
      status: 'denied',
      fanout: 0,
    });
    expect(authority.roomSequence(alice, bob)).toBe(0);
  });

  it('requires an active recipient for new direct sends and fans a self-send out once', () => {
    const authority = new ScenarioAuthorityModel();
    const alice = authority.freshLogin('Alice');
    const bob = authority.freshLogin('Bob');
    expect(authority.sendDirect(alice, 999, 'unknown', 'hello').status).toBe('denied');
    expect(authority.sendDirect(alice, bob, 'online', 'hello').status).toBe('accepted');
    authority.disconnect(bob);
    expect(authority.sendDirect(alice, bob, 'offline-new', 'hello').status).toBe('denied');
    const self = authority.sendDirect(alice, alice, 'self', 'note to self');
    expect(self).toMatchObject({ status: 'accepted', fanout: 1 });
  });

  it('does not deduplicate unkeyed sends and can resolve a saved retry after peer disconnect', () => {
    const authority = new ScenarioAuthorityModel();
    const alice = authority.freshLogin('Alice');
    const bob = authority.freshLogin('Bob');
    const first = authority.sendDirect(alice, bob, '', 'same text');
    const second = authority.sendDirect(alice, bob, '', 'same text');
    expect(first).toMatchObject({ status: 'accepted', sequence: 1, duplicate: false });
    expect(second).toMatchObject({ status: 'accepted', sequence: 2, duplicate: false });
    expect(authority.roomSequence(alice, bob)).toBe(2);

    const saved = authority.sendDirect(alice, bob, 'retry-after-drop', 'committed');
    authority.disconnect(bob);
    expect(authority.sendDirect(alice, bob, 'retry-after-drop', 'committed')).toMatchObject({
      status: 'accepted',
      sequence: saved.sequence,
      duplicate: true,
      fanout: 0,
    });
  });

  it('keeps group discovery separate from self-join and denies nonmember sends', () => {
    const authority = new ScenarioAuthorityModel();
    const alice = authority.freshLogin('Alice');
    const bob = authority.freshLogin('Bob');
    const charlie = authority.freshLogin('Charlie');
    authority.createGroup('group-1', alice);
    const snapshot = authority.discoverGroups();
    expect(snapshot[0].members).toEqual(['Alice']);
    expect(authority.isGroupMember('group-1', bob)).toBe(false);
    expect(authority.sendGroup('group-1', charlie, 'text-key', 'outside').status).toBe(
      'not-member',
    );
    expect(authority.joinGroup('group-1', bob)).toBe(true);
    expect(authority.sendGroup('group-1', bob, 'text-key', 'inside').status).toBe('accepted');
  });

  it('reports membership snapshots from each nonmember experiment state', () => {
    expect(experimentResult('nonmember', 0).members).toEqual(['alice']);
    expect(experimentResult('nonmember', 1).members).toEqual(['alice', 'bob']);
    expect(experimentResult('nonmember', 2).members).toEqual(['alice', 'bob']);
  });

  it('separates a reclaimed name from the old direct-history principal', () => {
    const authority = new ScenarioAuthorityModel();
    const oldBob = authority.freshLogin('Bob');
    const alice = authority.freshLogin('Alice');
    const room = authority.openDirect(alice, oldBob);
    expect(() => authority.freshLogin('Bob')).toThrow(/already active/i);
    authority.createGroup('group-1', oldBob);
    authority.disconnect(oldBob);
    expect(authority.isGroupMember('group-1', oldBob)).toBe(false);
    const replacement = authority.freshLogin('Bob');
    expect(replacement).not.toBe(oldBob);
    expect(authority.canReadDirect(room, replacement)).toBe(false);
    expect(authority.canReadDirect(room, oldBob)).toBe(true);
  });

  it('does not refresh liveness for fragments or outbound traffic and retains active write buffers', () => {
    expect(
      () => new SessionLifecycleModel({ idleTimeout: Number.NaN, maxFrames: 1, maxBytes: 10 }),
    ).toThrow();
    expect(
      () => new SessionLifecycleModel({ idleTimeout: 1.5, maxFrames: 1, maxBytes: 10 }),
    ).toThrow();
    const session = new SessionLifecycleModel({ idleTimeout: 30, maxFrames: 1, maxBytes: 100 });
    expect(() => session.advanceTime(Number.POSITIVE_INFINITY)).toThrow();
    expect(() => session.enqueueFrame(1.5)).toThrow();
    session.receivePartialBytes(2);
    session.sendOutbound(5);
    expect(session.lastActivity).toBe(0);
    expect(session.advanceTime(30)).toBe('closed');

    const writing = new SessionLifecycleModel({ idleTimeout: 30, maxFrames: 2, maxBytes: 100 });
    expect(writing.enqueueFrame(10)).toBe(true);
    writing.beginWrite();
    writing.close();
    expect(writing.activeBufferAlive).toBe(true);
    writing.completeWrite();
    expect(writing.activeBufferAlive).toBe(false);
  });
});
