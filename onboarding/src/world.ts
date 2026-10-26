/** Cinematic presentation only. Authorization and experiments live in scenario.ts. */
import * as T from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { SSAOPass } from 'three/addons/postprocessing/SSAOPass.js';
import { ANCHORS, cameraAt, revealAt, smooth } from './director';
import { annotations, membersAt } from './schematics';
import type { ChapterDefinition, BeatDefinition, ExperimentResult, Quality } from './types';

type Module = {
  root: T.Group;
  housing: T.Group;
  fallback: T.Group;
  position: T.Vector3;
  indicator: T.Mesh;
  kind: string;
};
type Label = { element: HTMLDivElement; point: T.Vector3; key: string };
const AMBER = 0xf4b46a,
  CYAN = 0x79daca,
  IVORY = 0xe7e3d9,
  RED = 0xf17b73;
const lerp = (a: number, b: number, t: number) => a + (b - a) * t;
const clamp = (x: number) => Math.min(1, Math.max(0, x));

export interface WorldFrame {
  chapter: ChapterDefinition;
  beat: BeatDefinition;
  time: number;
  landing: boolean;
  inspection: boolean;
  reducedMotion: boolean;
  experiment: ExperimentResult | null;
  experimentId: string | null;
  experimentStep: number;
}

export class World {
  readonly renderer: T.WebGLRenderer;
  readonly camera = new T.PerspectiveCamera(42, 1, 0.3, 180);
  readonly scene = new T.Scene();
  readonly composer: EffectComposer;
  readonly ready: Promise<void>;
  private bloom: UnrealBloomPass;
  private ao: SSAOPass | null = null;
  private modules: Record<string, Module> = {};
  private mats: T.Material[] = [];
  private labels: Label[] = [];
  private stage = new T.Group();
  private bytes = new T.Group();
  private secondFrame = new T.Group();
  private queue = new T.Group();
  private identities = new T.Group();
  private records = new T.Group();
  private membership = new T.Group();
  private clock = new T.Group();
  private token = new T.Group();
  private echo = new T.Group();
  private recipient = new T.Group();
  private ack = new T.Group();
  private paths: Record<string, T.CatmullRomCurve3> = {};
  private keyLight: T.DirectionalLight;
  private environment: T.Texture;
  private environmentTarget: T.WebGLRenderTarget;
  private loadingManager = new T.LoadingManager();
  private disposed = false;
  private revision = 0;
  private renderedKey = '';
  private quality: Quality = 'auto';
  private effectiveQuality: 'balanced' | 'high' = 'balanced';
  private inspectionYaw = 0.55;
  private inspectionPitch = 0.72;
  private inspectionDistance = 20;
  private inspectTarget = new T.Vector3();
  private dragging = false;
  private pointer = { x: 0, y: 0 };
  private inspection = false;
  private lastPhase = '';
  private lastStage = '';
  private width = 1;
  private height = 1;
  private lost = false;
  private frameTimes: number[] = [];
  private lastQualityBoundary = '';
  private diagramLabels: HTMLDivElement[] = [];
  private warm: T.MeshStandardMaterial | null = null;
  private cool: T.MeshStandardMaterial | null = null;
  private unreceived: T.MeshStandardMaterial | null = null;
  private warmedFrames = 0;
  private abort = new AbortController();
  private raycaster = new T.Raycaster();
  private pointerNDC = new T.Vector2();
  onSelect: ((key: string) => void) | null = null;
  onAssetError: ((message: string) => void) | null = null;

  constructor(
    private canvas: HTMLCanvasElement,
    private labelContainer: HTMLElement,
    onContext: (lost: boolean) => void,
  ) {
    this.renderer = new T.WebGLRenderer({
      canvas,
      antialias: true,
      powerPreference: 'high-performance',
      alpha: false,
    });
    this.renderer.outputColorSpace = T.SRGBColorSpace;
    this.renderer.toneMapping = T.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.1;
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = T.PCFShadowMap;
    this.scene.background = new T.Color(0x0b1013);
    this.scene.fog = new T.FogExp2(0x0b1013, 0.009);

    const pmrem = new T.PMREMGenerator(this.renderer);
    const room = new RoomEnvironment();
    this.environmentTarget = pmrem.fromScene(room, 0.035);
    this.environment = this.environmentTarget.texture;
    this.scene.environment = this.environment;
    this.scene.environmentIntensity = 0.72;
    room.dispose();
    pmrem.dispose();
    this.keyLight = new T.DirectionalLight(0xffdfb0, 4.1);
    this.keyLight.position.set(-8, 24, 11);
    this.keyLight.castShadow = true;
    this.keyLight.shadow.mapSize.set(2048, 2048);
    Object.assign(this.keyLight.shadow.camera, {
      left: -25,
      right: 25,
      top: 25,
      bottom: -25,
      near: 1,
      far: 65,
    });
    this.keyLight.shadow.bias = -0.00015;
    this.keyLight.shadow.normalBias = 0.035;
    this.scene.add(this.keyLight, this.keyLight.target);
    const rim = new T.DirectionalLight(0xa8d9f0, 3);
    rim.position.set(12, 9, -18);
    this.scene.add(rim);
    const fill = new T.HemisphereLight(0xe8f1fa, 0x17100a, 1.1);
    this.scene.add(fill);

    const floor = new T.Mesh(new T.PlaneGeometry(250, 250), this.material(0x0b1013, 0.18, 0.52));
    floor.rotation.x = -Math.PI / 2;
    floor.position.y = -0.32;
    floor.receiveShadow = true;
    this.scene.add(floor);
    const stageMat = this.material(0x11191c, 0.65, 0.34);
    const base = this.box(43, 0.24, 35, stageMat, 0.35);
    base.position.set(0, -0.16, 0);
    this.scene.add(base);
    // Subtle parallel engraved lines establish scale without a distracting infinite grid.
    const grid = new T.Group();
    for (let i = -20; i <= 20; i += 4) {
      const line = new T.Mesh(new T.BoxGeometry(0.012, 0.005, 34), this.basic(0x233135));
      line.position.set(i, -0.031, 0);
      grid.add(line);
    }
    this.scene.add(grid);

    this.addModule('alice', 'client', new T.Vector3(-12, 0, 4), 0.66, AMBER);
    this.addModule('server', 'server', new T.Vector3(0, 0, 0), 1.16, AMBER);
    this.addModule('bob', 'client', new T.Vector3(12, 0, -4), 0.66, CYAN);
    this.addModule('charlie', 'client', new T.Vector3(13, 0, 10), 0.66, 0x73858b);
    this.addModule('storage', 'storage', new T.Vector3(0, 0, -12), 0.82, CYAN);

    this.addPath(
      'in',
      [
        [-12, 0.22, 4],
        [-8, 0.22, 4],
        [-5, 0.22, 0],
        [0, 0.22, 0],
      ],
      AMBER,
    );
    this.addPath(
      'bob',
      [
        [0, 0.22, 0],
        [5, 0.22, 0],
        [8, 0.22, -4],
        [12, 0.22, -4],
      ],
      CYAN,
    );
    this.addPath(
      'charlie',
      [
        [0, 0.22, 0],
        [7, 0.22, 0],
        [9, 0.22, 10],
        [13, 0.22, 10],
      ],
      0x405359,
    );
    this.addPath(
      'store',
      [
        [0, 0.22, 0],
        [0, 0.22, -4],
        [0, 0.22, -8],
        [0, 0.22, -12],
      ],
      0x608b87,
    );
    this.scene.add(this.stage);
    this.buildDiagrams();
    this.token = this.makeToken(AMBER);
    this.echo = this.makeToken(AMBER);
    this.recipient = this.makeToken(CYAN);
    this.ack = this.makeToken(IVORY, true);
    this.scene.add(this.token, this.echo, this.recipient, this.ack);
    this.addLabel('alice', 'ALICE', 'SENDER', new T.Vector3(-12, 2.8, 4));
    this.addLabel('server', 'CHAT SERVER', 'AUTHORITATIVE', new T.Vector3(0, 4.8, 0));
    this.addLabel('bob', 'BOB', 'RECIPIENT', new T.Vector3(12, 2.8, -4));
    this.addLabel(
      'charlie',
      'CHARLIE',
      'CONNECTED · OUTSIDE THIS ROOM',
      new T.Vector3(13, 2.8, 10),
    );
    this.addLabel('storage', 'SQLITE / WAL', 'COMMIT BEFORE ACK', new T.Vector3(0, 3, -12));
    for (let i = 0; i < 3; i++) {
      const el = document.createElement('div');
      el.className = 'diagram-label';
      el.hidden = true;
      this.labelContainer.append(el);
      this.diagramLabels.push(el);
    }

    this.composer = new EffectComposer(this.renderer);
    this.composer.addPass(new RenderPass(this.scene, this.camera));
    this.bloom = new UnrealBloomPass(new T.Vector2(1024, 768), 0.24, 0.45, 1.3);
    this.composer.addPass(this.bloom);
    this.composer.addPass(new OutputPass());
    this.resize();
    const signal = this.abort.signal;
    window.addEventListener('resize', () => this.resize(), { signal });
    canvas.addEventListener(
      'pointerdown',
      (e) => {
        if (!this.inspection) return;
        this.dragging = true;
        this.pointer = { x: e.clientX, y: e.clientY };
        canvas.setPointerCapture(e.pointerId);
      },
      { signal },
    );
    canvas.addEventListener(
      'pointermove',
      (e) => {
        if (!this.dragging || !this.inspection) return;
        this.inspectionYaw -= (e.clientX - this.pointer.x) * 0.007;
        this.inspectionPitch = Math.max(
          0.22,
          Math.min(1.35, this.inspectionPitch + (e.clientY - this.pointer.y) * 0.006),
        );
        this.pointer = { x: e.clientX, y: e.clientY };
      },
      { signal },
    );
    canvas.addEventListener(
      'pointerup',
      () => {
        this.dragging = false;
      },
      { signal },
    );
    canvas.addEventListener(
      'pointercancel',
      () => {
        this.dragging = false;
      },
      { signal },
    );
    canvas.addEventListener(
      'wheel',
      (e) => {
        if (!this.inspection) return;
        e.preventDefault();
        this.inspectionDistance = Math.max(
          10,
          Math.min(50, this.inspectionDistance + e.deltaY * 0.018),
        );
      },
      { passive: false, signal },
    );
    canvas.addEventListener(
      'dblclick',
      (e) => {
        const rect = canvas.getBoundingClientRect();
        this.pointerNDC.set(
          ((e.clientX - rect.left) / rect.width) * 2 - 1,
          (-(e.clientY - rect.top) / rect.height) * 2 + 1,
        );
        this.raycaster.setFromCamera(this.pointerNDC, this.camera);
        const hit = this.raycaster.intersectObjects(
          Object.values(this.modules).map((m) => m.root),
          true,
        )[0];
        if (hit) {
          let o: T.Object3D | null = hit.object;
          while (o && !o.userData.module) o = o.parent;
          if (o) this.onSelect?.(o.userData.module);
        }
      },
      { signal },
    );
    canvas.addEventListener(
      'webglcontextlost',
      (e) => {
        e.preventDefault();
        this.lost = true;
        onContext(true);
      },
      { signal },
    );
    canvas.addEventListener(
      'webglcontextrestored',
      () => {
        this.lost = false;
        onContext(false);
        this.resize();
      },
      { signal },
    );
    this.ready = this.loadAssets();
  }

  private material(
    color: number,
    metalness = 0.5,
    roughness = 0.4,
    emissive = 0,
    intensity = 0,
  ): T.MeshStandardMaterial {
    const m = new T.MeshStandardMaterial({
      color,
      metalness,
      roughness,
      emissive,
      emissiveIntensity: intensity,
    });
    this.mats.push(m);
    return m;
  }
  private basic(color: number): T.MeshBasicMaterial {
    const m = new T.MeshBasicMaterial({ color });
    this.mats.push(m);
    return m;
  }
  private box(w: number, h: number, d: number, material: T.Material, radius = 0.1): T.Mesh {
    const mesh = new T.Mesh(
      new RoundedBoxGeometry(w, h, d, 2, Math.min(radius, h / 3, w / 3, d / 3)),
      material,
    );
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    return mesh;
  }
  private addModule(id: string, kind: string, position: T.Vector3, scale: number, accent: number) {
    const root = new T.Group();
    root.position.copy(position);
    root.userData.module = id;
    const housing = new T.Group();
    housing.scale.setScalar(scale);
    root.add(housing);
    const fallback = new T.Group();
    const graphite = this.material(0x28343a, 0.8, 0.32),
      ceramic = this.material(0x131b20, 0.3, 0.47);
    const shell = this.box(6, 0.6, 4, graphite, 0.2);
    shell.name = 'shell';
    shell.position.y = 0.45;
    fallback.add(shell);
    for (let i = 0; i < 4; i++) {
      const tray = this.box(5.4, 0.16, 3.4, i % 2 ? graphite : ceramic);
      tray.position.y = 0.9 + i * 0.27;
      tray.name = `tray-${i}`;
      tray.userData.restY = tray.position.y;
      fallback.add(tray);
      const band = this.box(4.8, 0.035, 0.05, this.material(accent, 0.4, 0.3, accent, 1.8));
      band.position.set(0, 0.1, 1.72);
      tray.add(band);
    }
    const lid = this.box(6, 0.3, 4, graphite, 0.15);
    lid.position.y = 2.1;
    lid.name = 'lid';
    lid.userData.restY = 2.1;
    fallback.add(lid);
    housing.add(fallback);
    const platform = this.box(
      7 * scale + 0.8,
      0.12,
      5 * scale + 0.6,
      this.material(0x111c21, 0.72, 0.3),
    );
    platform.position.y = 0.03;
    root.add(platform);
    const indicator = this.box(
      2.6 * scale,
      0.025,
      0.055,
      this.material(accent, 0.2, 0.3, accent, 2.8),
    );
    indicator.position.set(0, 0.12, 2.5 * scale + 0.2);
    root.add(indicator);
    this.modules[id] = { root, housing, fallback, position, indicator, kind };
    this.scene.add(root);
  }
  private async loadAssets() {
    const loader = new GLTFLoader(this.loadingManager);
    const results = await Promise.allSettled(
      ['client', 'server', 'storage'].map(async (kind) => {
        const gltf = await loader.loadAsync(`${import.meta.env.BASE_URL}assets/models/${kind}.glb`);
        if (this.disposed) {
          gltf.scene.traverse((o) => {
            if (o instanceof T.Mesh) {
              o.geometry.dispose();
              for (const mat of Array.isArray(o.material) ? o.material : [o.material])
                mat.dispose();
            }
          });
          return;
        }
        for (const m of Object.values(this.modules).filter((m) => m.kind === kind)) {
          const model = gltf.scene.clone(true);
          model.traverse((o) => {
            if (o instanceof T.Mesh) {
              o.castShadow = true;
              o.receiveShadow = true;
            }
            o.userData.restY = o.position.y;
          });
          m.fallback.visible = false;
          m.housing.add(model);
          this.revision++;
        }
      }),
    );
    const failures = results.filter((r) => r.status === 'rejected');
    if (failures.length && !this.disposed)
      this.onAssetError?.(
        'Some model assets could not load. The functional cutaway remains available.',
      );
  }
  private addPath(id: string, points: [number, number, number][], accent: number) {
    const curve = new T.CatmullRomCurve3(
      points.map((p) => new T.Vector3(...p)),
      false,
      'centripetal',
    );
    this.paths[id] = curve;
    const cable = new T.Mesh(
      new T.TubeGeometry(curve, 40, 0.11, 6, false),
      this.material(0x131c20, 0.4, 0.4),
    );
    const trace = new T.Mesh(
      new T.TubeGeometry(curve, 40, 0.018, 5, false),
      this.material(accent, 0.3, 0.4, accent, 0.65),
    );
    trace.position.y = 0.12;
    this.scene.add(cable, trace);
  }
  private makeToken(color: number, receipt = false): T.Group {
    const g = new T.Group();
    const shell = this.box(
      receipt ? 0.68 : 1.05,
      0.16,
      receipt ? 0.68 : 0.8,
      this.material(0xc2cbc9, 0.7, 0.2),
    );
    const core = this.box(
      receipt ? 0.48 : 0.8,
      0.035,
      receipt ? 0.48 : 0.56,
      this.material(color, 0.2, 0.35, color, 3),
    );
    core.position.y = 0.11;
    g.add(shell, core);
    return g;
  }
  private addLabel(key: string, title: string, subtitle: string, point: T.Vector3) {
    const el = document.createElement('div');
    el.className = `world-label label-${key}`;
    const name = document.createElement('strong');
    name.textContent = title;
    const sub = document.createElement('span');
    sub.textContent = subtitle;
    el.append(name, sub);
    this.labelContainer.append(el);
    this.labels.push({ element: el, point, key });
  }
  private buildDiagrams() {
    const dark = this.material(0x1e2a30, 0.75, 0.32),
      edge = this.material(0x637477, 0.7, 0.3);
    const warm = this.material(AMBER, 0.25, 0.3, AMBER, 0.7),
      cool = this.material(CYAN, 0.3, 0.3, CYAN, 0.7);
    this.warm = warm;
    this.cool = cool;
    this.unreceived = dark;
    // A frame is four prefix bytes followed by an ordered payload, never a machine.
    for (let i = 0; i < 24; i++) {
      const tile = this.box(0.35, 0.24, 0.95, i < 4 ? warm : cool, 0.05);
      tile.position.set((i - 11.5) * 0.43, 0, 0);
      this.bytes.add(tile);
    }
    const frameBase = this.box(10.6, 0.12, 1.65, dark);
    frameBase.position.y = -0.23;
    this.bytes.add(frameBase);
    this.bytes.position.set(0, 4.3, 2);
    this.stage.add(this.bytes);
    this.secondFrame = this.bytes.clone(true);
    this.secondFrame.position.z = 4.3;
    this.stage.add(this.secondFrame);
    // Eight visual positions represent a bounded queue; precise limits live in labels/metrics.
    for (let i = 0; i < 8; i++) {
      const slot = this.box(0.6, 0.08, 1.9, edge);
      slot.position.set((i - 3.5) * 0.82, -0.12, 0);
      this.queue.add(slot);
      const packet = this.box(0.52, 0.4, 1.4, i === 7 ? cool : warm);
      packet.position.set((i - 3.5) * 0.82, 0.2, 0);
      packet.name = `queued-${i}`;
      this.queue.add(packet);
    }
    this.queue.position.set(0, 4.5, 1);
    this.stage.add(this.queue);
    for (let i = 0; i < 3; i++) {
      const card = this.box(2.2, 0.15, 2.8, i === 2 ? edge : i === 0 ? warm : cool);
      card.position.set((i - 1) * 2.7, 0, 0);
      this.identities.add(card);
      for (let j = 0; j < 3; j++) {
        const line = this.box(1.5 - j * 0.2, 0.015, 0.08, dark);
        line.position.set(0, 0.09, -0.7 + j * 0.5);
        card.add(line);
      }
    }
    this.identities.position.set(0, 4.4, 1);
    this.stage.add(this.identities);
    for (let i = 0; i < 7; i++) {
      const record = this.box(0.75, 0.22, 2, i % 2 ? cool : warm);
      record.position.set((i - 3) * 0.95, 0, 0);
      record.name = `record-${i + 1}`;
      this.records.add(record);
    }
    this.records.position.set(0, 4, -12);
    this.stage.add(this.records);
    const ring = new T.Mesh(new T.TorusGeometry(3.4, 0.035, 6, 80), cool);
    ring.rotation.x = -Math.PI / 2;
    this.membership.add(ring);
    for (let i = 0; i < 3; i++) {
      const disk = new T.Mesh(
        new T.CylinderGeometry(0.52, 0.52, 0.18, 32),
        i === 0 ? warm : i === 1 ? cool : dark,
      );
      disk.position.set(i === 0 ? -1.4 : i === 1 ? 1.4 : 4.8, 0, 0);
      this.membership.add(disk);
    }
    this.membership.position.set(0, 4.4, 0);
    this.stage.add(this.membership);
    const face = new T.Mesh(new T.CylinderGeometry(2.2, 2.2, 0.2, 64), dark);
    this.clock.add(face);
    for (let i = 0; i < 30; i++) {
      const tick = this.box(0.035, 0.05, i % 5 === 0 ? 0.3 : 0.15, i % 10 === 0 ? warm : edge);
      const a = (i / 30) * Math.PI * 2;
      tick.position.set(Math.sin(a) * 2, 0.13, Math.cos(a) * 2);
      tick.rotation.y = a;
      this.clock.add(tick);
    }
    const hand = this.box(0.06, 0.05, 1.7, cool);
    hand.position.set(0, 0.2, 0.7);
    const pivot = new T.Group();
    pivot.name = 'clock-hand';
    pivot.add(hand);
    this.clock.add(pivot);
    this.clock.position.set(0, 4.5, 0);
    this.stage.add(this.clock);
  }

  setQuality(q: Quality) {
    this.quality = q;
    this.applyQuality(q === 'high' ? 'high' : 'balanced');
  }
  private applyQuality(q: 'balanced' | 'high') {
    this.effectiveQuality = q;
    if (q === 'high' && !this.ao) {
      this.ao = new SSAOPass(this.scene, this.camera, this.width, this.height, 8);
      this.ao.kernelRadius = 1.5;
      this.ao.minDistance = 0.002;
      this.ao.maxDistance = 0.12;
      this.composer.insertPass(this.ao, 1);
    }
    if (this.ao) this.ao.enabled = q === 'high';
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, q === 'high' ? 1.75 : 1.25));
    this.bloom.enabled = q === 'high';
    this.resize();
  }
  resize() {
    this.revision++;
    const rect = this.canvas.getBoundingClientRect();
    this.width = Math.max(1, rect.width);
    this.height = Math.max(1, rect.height);
    this.camera.aspect = this.width / this.height;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(this.width, this.height, false);
    this.composer?.setSize(this.width, this.height);
  }
  recordFrame(ms: number) {
    if (!Number.isFinite(ms) || ms <= 0 || ms > 250) return;
    if (this.warmedFrames++ > 45) {
      this.frameTimes.push(ms);
      if (this.frameTimes.length > 600) this.frameTimes.shift();
    }
  }
  metrics() {
    const times = [...this.frameTimes].sort((a, b) => a - b);
    return {
      quality: this.effectiveQuality,
      samples: times.length,
      medianMs: times[Math.floor(times.length * 0.5)] ?? 0,
      p95Ms: times[Math.floor(times.length * 0.95)] ?? 0,
      drawCalls: this.renderer.info.render.calls,
      triangles: this.renderer.info.render.triangles,
      geometries: this.renderer.info.memory.geometries,
      textures: this.renderer.info.memory.textures,
      pixelRatio: this.renderer.getPixelRatio(),
    };
  }

  render(frame: WorldFrame) {
    if (this.lost || this.disposed) return false;
    const renderKey = [
      this.revision,
      frame.chapter.id,
      frame.beat.id,
      frame.reducedMotion ? frame.beat.start : frame.time,
      frame.landing,
      frame.inspection,
      frame.reducedMotion,
      frame.experimentId,
      frame.experimentStep,
      this.inspectionYaw,
      this.inspectionPitch,
      this.inspectionDistance,
    ].join('/');
    if (renderKey === this.renderedKey) return false;
    this.renderedKey = renderKey;
    const { chapter, beat, time, experiment, experimentId } = frame;
    const rawProgress = clamp((time - beat.start) / Math.max(1, beat.end - beat.start));
    const progress = frame.reducedMotion ? 1 : rawProgress;
    const phase = beat.phase.toLowerCase();
    const boundary = `${chapter.id}/${beat.id}`;
    if (boundary !== this.lastQualityBoundary) {
      this.lastQualityBoundary = boundary;
      if (
        this.quality === 'auto' &&
        this.frameTimes.length > 120 &&
        this.metrics().p95Ms > 34 &&
        this.renderer.getPixelRatio() > 0.9
      ) {
        this.renderer.setPixelRatio(0.9);
        this.resize();
      }
    }
    const framing =
      chapter.stage === 'framing' ||
      (chapter.stage === 'overview' &&
        beat.focus === 'frame' &&
        ['encode', 'split'].includes(phase)) ||
      ['split-header', 'coalesced', 'oversized'].includes(experimentId ?? '');
    const queueing =
      chapter.stage === 'session' ||
      (chapter.stage === 'client' && beat.focus === 'queue') ||
      ['slow-receiver', 'cancel-write'].includes(experimentId ?? '');
    const group = chapter.stage === 'groups' || experimentId === 'nonmember';
    const recovering = chapter.stage === 'recovery' || experimentId === 'lost-ack';
    const identity = ['identity', 'tls'].includes(chapter.stage) || experimentId === 'reclaim-name';
    const timing = chapter.stage === 'heartbeat' || experimentId === 'partial-idle';
    this.bytes.visible = !frame.landing && framing;
    this.secondFrame.visible =
      this.bytes.visible && (experimentId === 'coalesced' || phase === 'complete');
    this.queue.visible = !frame.landing && queueing;
    this.queue.position.set(
      chapter.stage === 'client' ? -12 : 0,
      4.5,
      chapter.stage === 'client' ? 4 : 1,
    );
    this.identities.visible =
      !frame.landing &&
      (identity ||
        (chapter.stage === 'client' && !queueing) ||
        (chapter.stage === 'routing' && ['encode', 'authorize', 'verify'].includes(phase)) ||
        experimentId === 'forged-sender');
    this.identities.position.set(
      chapter.stage === 'client' ? -12 : 0,
      4.4,
      chapter.stage === 'client' ? 4 : 1,
    );
    this.membership.visible = !frame.landing && group;
    this.records.visible =
      !frame.landing &&
      (chapter.stage === 'storage' ||
        recovering ||
        (chapter.stage === 'routing' && phase === 'commit'));
    this.clock.visible = !frame.landing && timing;
    this.records.position.z = recovering && beat.focus !== 'history' ? 1 : -12;

    const explosion = frame.landing ? 0.3 : revealAt(chapter, time, frame.reducedMotion);
    for (const [id, m] of Object.entries(this.modules)) {
      const selected = frame.landing
        ? id === 'server'
        : beat.focus === id ||
          (id === 'alice' &&
            (beat.focus === 'client' || (chapter.stage === 'client' && beat.focus === 'queue'))) ||
          (id === 'server' &&
            ['frame', 'queue', 'identity', 'group', 'clock', 'recovery'].includes(beat.focus) &&
            !(chapter.stage === 'client' && beat.focus === 'queue')) ||
          (id === 'storage' && beat.focus === 'history');
      m.housing.traverse((o) => {
        if (o instanceof T.Mesh)
          o.castShadow = selected || (id === 'server' && (beat.view === 'wide' || frame.landing));
        if (o.userData.restY === undefined) return;
        const i = o.name.match(/^tray-(\d)$/);
        if (o.name === 'lid')
          o.position.y =
            o.userData.restY +
            (selected || (frame.landing && id === 'server') ? explosion * 4.3 : 0);
        if (i)
          o.position.y = o.userData.restY + (selected ? explosion * (Number(i[1]) + 1) * 0.65 : 0);
      });
      const mat = m.indicator.material as T.MeshStandardMaterial;
      mat.emissiveIntensity = id === 'charlie' ? 0.45 : selected ? 3 : 1;
      m.root.visible = true;
    }
    // State-bearing diagrams reconstruct directly from the current semantic beat/result.
    if (framing) {
      const received =
        experiment?.receivedBytes ??
        (phase === 'split'
          ? 2
          : phase === 'waiting'
            ? 12
            : phase === 'complete' || phase === 'close'
              ? 24
              : 4);
      this.bytes.children.forEach((o, i) => {
        if (i >= 24 || !(o instanceof T.Mesh)) return;
        o.position.y = i < received ? 0.18 : 0;
        o.material = i < received ? (i < 4 ? this.warm! : this.cool!) : this.unreceived!;
        o.position.z = experiment?.status === 'rejected' && i >= 4 ? 3 : 0;
      });
      this.bytes.rotation.y = 0;
    }
    if (queueing) {
      const count =
        experiment?.queue ??
        (phase.includes('close') || phase.includes('cancel') ? 0 : phase.includes('queue') ? 6 : 3);
      this.queue.children.forEach((o) => {
        const m = o.name.match(/^queued-(\d)$/);
        if (m) o.visible = Number(m[1]) < count;
      });
      const active = this.queue.getObjectByName('queued-7');
      if (active && experiment?.activeBufferAlive) active.visible = true;
    }
    if (group) {
      const bob = this.membership.children[2];
      const joined = membersAt(beat, experiment).includes('bob');
      if (bob) bob.position.x = joined ? 1.4 : 4.8;
      if (this.membership.children[3]) this.membership.children[3].position.x = joined ? 4.8 : 6.3;
    }
    if (recovering)
      this.records.children.forEach((o, i) => {
        o.visible = !experiment?.sequence || experiment.sequence.includes(i + 1);
        if (!experiment)
          o.visible =
            phase.includes('replay') ||
            phase.includes('retry') ||
            phase.includes('received') ||
            phase.includes('complete') ||
            i < 3 ||
            i === 6;
      });
    if (timing) {
      const hand = this.clock.getObjectByName('clock-hand');
      if (hand)
        hand.rotation.y =
          -(experiment ? (frame.experimentStep === 0 ? 10 / 30 : 1) : progress) * Math.PI * 2;
    }

    const isDelivery =
      /deliver|received|echo|fanout|complete|finish/.test(phase) ||
      (chapter.stage === 'routing' && ['deny', 'ack'].includes(phase));
    const isCommitted = /commit|ack|deliver|received|echo|fanout|complete|finish|replay|retry/.test(
      phase,
    );
    const denied = experiment && ['rejected', 'closed'].includes(experiment.status);
    this.token.visible = !denied && !framing && !queueing && !identity && !timing;
    this.echo.visible = !frame.landing && !denied && isDelivery;
    this.recipient.visible = this.echo.visible;
    this.ack.visible =
      !frame.landing &&
      !denied &&
      isCommitted &&
      !['heartbeat', 'tls', 'identity', 'client'].includes(chapter.stage);
    const travel =
      phase === 'deliver' || phase === 'submit' || phase === 'ack' ? smooth(progress) : 1;
    this.token.position.copy(
      this.paths.in.getPointAt(
        frame.landing || phase === 'waiting' ? 0.12 : phase === 'submit' ? smooth(progress) : 1,
      ),
    ).y += 0.7;
    this.echo.position.copy(this.paths.in.getPointAt(1 - (isDelivery ? travel : 0))).y += 0.7;
    this.recipient.position.copy(this.paths.bob.getPointAt(isDelivery ? travel : 0)).y += 0.7;
    this.ack.position.copy(this.paths.in.getPointAt(1 - travel)).y += 1.2;
    if (experiment?.deliveries) {
      this.echo.visible =
        experiment.deliveries.includes('Alice') || experiment.deliveries.includes('alice');
      this.recipient.visible =
        experiment.deliveries.includes('Bob') || experiment.deliveries.includes('bob');
      if (this.echo.visible) this.echo.position.set(-12, 2.2, 4);
      if (this.recipient.visible) this.recipient.position.set(12, 2.2, -4);
    }

    const camera = cameraAt(
      chapter,
      time,
      this.width / this.height,
      frame.reducedMotion,
      frame.landing,
    );
    if (frame.inspection && !this.inspection) {
      this.inspectTarget.copy(camera.target);
      this.inspectionDistance = camera.position.distanceTo(camera.target);
      this.inspectionYaw = Math.atan2(
        camera.position.x - camera.target.x,
        camera.position.z - camera.target.z,
      );
      this.inspectionPitch = Math.atan2(
        camera.position.y - camera.target.y,
        Math.hypot(camera.position.x - camera.target.x, camera.position.z - camera.target.z),
      );
    }
    this.inspection = frame.inspection;
    if (frame.inspection) {
      this.camera.position
        .copy(this.inspectTarget)
        .add(
          new T.Vector3(
            Math.sin(this.inspectionYaw) * Math.cos(this.inspectionPitch) * this.inspectionDistance,
            Math.sin(this.inspectionPitch) * this.inspectionDistance,
            Math.cos(this.inspectionYaw) * Math.cos(this.inspectionPitch) * this.inspectionDistance,
          ),
        );
      this.camera.lookAt(this.inspectTarget);
    } else {
      this.camera.position.copy(camera.position);
      this.camera.lookAt(camera.target);
      this.camera.fov = camera.fov;
    }
    this.camera.updateProjectionMatrix();
    this.scene.updateMatrixWorld();
    this.camera.updateMatrixWorld(true);
    this.updateLabels(frame);
    this.updateAnnotations(frame);
    // Shadow transforms only change with authored subject boundaries, avoiding swimming.
    if (this.lastStage !== chapter.stage || this.lastPhase !== `${beat.focus}/${beat.view}`) {
      this.lastStage = chapter.stage;
      this.lastPhase = `${beat.focus}/${beat.view}`;
      const target = ANCHORS[beat.focus] ?? ANCHORS.server;
      this.keyLight.target.position.set(...target);
      this.keyLight.position.set(target[0] - 8, 24, target[2] + 11);
      const bounds = beat.view === 'wide' || frame.landing ? 25 : 11;
      Object.assign(this.keyLight.shadow.camera, {
        left: -bounds,
        right: bounds,
        top: bounds,
        bottom: -bounds,
      });
      this.keyLight.shadow.camera.updateProjectionMatrix();
    }
    this.renderer.info.autoReset = false;
    this.renderer.info.reset();
    this.composer.render(0);
    return true;
  }
  private updateLabels(frame: WorldFrame) {
    for (const label of this.labels) {
      const p = label.point.clone().project(this.camera);
      const x = (p.x * 0.5 + 0.5) * this.width,
        y = (-p.y * 0.5 + 0.5) * this.height;
      const wide = frame.landing || frame.beat.view === 'wide';
      const selected =
        label.key === frame.beat.focus ||
        (label.key === 'alice' && frame.beat.focus === 'client') ||
        (label.key === 'storage' && frame.beat.focus === 'history');
      const landingRect = document.querySelector('.landing')?.getBoundingClientRect();
      const inTitle =
        frame.landing && landingRect
          ? x > landingRect.left - 70 &&
            x < landingRect.right + 70 &&
            y > landingRect.top - 30 &&
            y < landingRect.bottom + 30
          : x < Math.min(this.width * 0.4, 460) && y < this.height * 0.47;
      const visible =
        p.z < 1 &&
        x > 70 &&
        x < this.width - 70 &&
        y > 100 &&
        y < this.height - 185 &&
        !inTitle &&
        (wide || selected || (label.key === 'charlie' && frame.chapter.stage === 'routing'));
      label.element.hidden = !visible;
      label.element.style.transform = `translate(${x}px, ${y}px) translate(-50%, -100%)`;
    }
  }
  private updateAnnotations(frame: WorldFrame) {
    const specs = frame.landing
      ? []
      : annotations(frame.chapter, frame.beat, frame.experiment, frame.experimentId);
    this.diagramLabels.forEach((el, i) => {
      const spec = specs[i];
      if (!spec) {
        el.hidden = true;
        return;
      }
      const p = new T.Vector3(...spec.at).project(this.camera);
      const x = (p.x * 0.5 + 0.5) * this.width,
        y = (-p.y * 0.5 + 0.5) * this.height;
      el.hidden = p.z > 1 || x < 50 || x > this.width - 50 || y < 100 || y > this.height - 135;
      if (frame.inspection && x < 400) el.hidden = true;
      el.dataset.tone = spec.tone ?? 'cyan';
      if (el.dataset.value !== spec.value || el.dataset.title !== spec.title) {
        const title = document.createElement('span');
        title.textContent = spec.title;
        const value = document.createElement('strong');
        value.textContent = spec.value;
        el.replaceChildren(title, value);
        el.dataset.value = spec.value;
        el.dataset.title = spec.title;
      }
      el.style.transform = `translate(${x}px,${y}px) translate(-50%,-100%)`;
    });
  }
  dispose() {
    if (this.disposed) return;
    this.disposed = true;
    this.loadingManager.abort();
    this.abort.abort();
    this.labels.forEach((l) => l.element.remove());
    this.diagramLabels.forEach((l) => l.remove());
    const geometries = new Set<T.BufferGeometry>(),
      materials = new Set<T.Material>(this.mats),
      textures = new Set<T.Texture>();
    this.scene.traverse((o) => {
      if (!(o instanceof T.Mesh)) return;
      geometries.add(o.geometry);
      for (const m of Array.isArray(o.material) ? o.material : [o.material]) materials.add(m);
    });
    materials.forEach((m) => {
      for (const v of Object.values(m)) if (v instanceof T.Texture) textures.add(v);
      m.dispose();
    });
    geometries.forEach((g) => g.dispose());
    textures.forEach((t) => t.dispose());
    this.environmentTarget.dispose();
    this.keyLight.shadow.dispose();
    this.composer.passes.forEach((p) => p.dispose());
    this.composer.dispose();
    this.renderer.dispose();
  }
}
