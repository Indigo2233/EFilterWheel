#!/usr/bin/env python3
"""
STL 尺寸分析与 STEP 转换工具
- 提取每个 STL 的精确尺寸、孔径、特征
- 生成 CSV 尺寸汇总表
- 如果 cadquery 可用，尝试自动重建并导出 STEP

用法:
  python stl2step.py                    # 全部转换
  python stl2step.py --single 外壳.STL   # 单个
  python stl2step.py --csv               # 仅生成 CSV 尺寸表

依赖: pip install trimesh numpy-stl scipy
可选: pip install cadquery (Python ≤3.11)
"""

import os
import sys
import math
import csv
import json
import argparse
from pathlib import Path
from datetime import datetime

import numpy as np
import trimesh
from scipy.spatial import ConvexHull

# ============================================================================
# 路径
# ============================================================================
STL_DIR  = Path("ref/3D打印模型文件")
STEP_DIR = Path("ref/step")
CSV_PATH = STEP_DIR / "dimensions.csv"

# 尝试加载 cadquery (可选)
CADQUERY_OK = False
try:
    import cadquery as cq
    CADQUERY_OK = True
except ImportError:
    pass

# ============================================================================
# 尺寸提取
# ============================================================================

def load_and_merge(stl_path: Path) -> trimesh.Trimesh | None:
    """加载 STL, 合并多 mesh 场景"""
    try:
        obj = trimesh.load(str(stl_path), force="mesh")
    except Exception as e:
        print(f"    ❌ 加载失败: {e}")
        return None

    if isinstance(obj, trimesh.Scene):
        geoms = list(obj.geometry.values())
        meshes = [g for g in geoms if isinstance(g, trimesh.Trimesh)]
        if not meshes:
            return None
        return trimesh.util.concatenate(meshes)
    elif isinstance(obj, trimesh.Trimesh):
        return obj
    return None


def extract_all(mesh: trimesh.Trimesh) -> dict:
    """提取完整尺寸信息"""
    d = {}

    # 包围盒
    lo, hi = mesh.bounds
    size = hi - lo
    d["bbox_lo"] = lo.tolist()
    d["bbox_hi"] = hi.tolist()
    d["size_x"] = round(float(size[0]), 2)
    d["size_y"] = round(float(size[1]), 2)
    d["size_z"] = round(float(size[2]), 2)
    d["volume"]  = round(float(mesh.volume) if hasattr(mesh, "volume") else 0, 2)
    d["area"]    = round(float(mesh.area) if hasattr(mesh, "area") else 0, 2)
    d["vertices"] = len(mesh.vertices)
    d["faces"]    = len(mesh.faces)

    # 零件类型
    dx, dy, dz = size
    dims_sorted = sorted([dx, dy, dz])
    thick = dims_sorted[0]
    max_d = dims_sorted[2]
    mid_d = dims_sorted[1]

    if thick < max_d * 0.15:
        # 薄板
        d["part_type"] = "round_disk" if abs(dx - dy) < max(dx, dy) * 0.15 else "flat_plate"
    elif abs(mid_d - max_d) < max(mid_d, max_d) * 0.25 and thick > max_d * 0.25:
        # 两个维度相近 → 圆柱/圆盘
        d["part_type"] = "cylinder"
    elif thick > max_d * 0.4:
        # 三个维度都接近 → 盒体
        d["part_type"] = "box"
    else:
        d["part_type"] = "generic"

    # 投影轮廓 (Z 轴)
    verts_2d = mesh.vertices[:, :2]
    try:
        hull = ConvexHull(verts_2d)
        d["profile_radius"] = round(float(np.max(np.linalg.norm(
            verts_2d[hull.vertices] - np.mean(verts_2d[hull.vertices], axis=0), axis=1))), 2)
    except Exception:
        d["profile_radius"] = round(float(max(dx, dy) / 2), 2)

    # 检测圆孔 (分析法线平行于 Z 的 faces 聚类)
    holes = detect_holes_robust(mesh, d["size_z"])
    d["holes"] = holes
    d["hole_count"] = len(holes)

    # 壁厚估计 (仅在可能为空腔的零件上)
    if d["part_type"] in ("box", "shallow_box"):
        wt = estimate_wall_thickness(mesh, size)
        if wt is not None:
            d["wall_thickness"] = wt

    return d


def detect_holes_robust(mesh: trimesh.Trimesh, thickness: float) -> list[dict]:
    """鲁棒的圆孔检测: 在顶面/底面找圆形缺口"""
    holes = []

    # 分析顶面 (Z ≈ max) 和底面 (Z ≈ min)
    z_min, z_max = mesh.bounds[0][2], mesh.bounds[1][2]

    for face_z, label in [(z_max, "top"), (z_min, "bottom")]:
        # 取靠近该平面的顶点
        tol = thickness * 0.05
        mask = np.abs(mesh.vertices[:, 2] - face_z) < tol
        face_verts = mesh.vertices[mask]

        if len(face_verts) < 10:
            continue

        # 投影到 XY, 聚类检测环形间隙
        xy = face_verts[:, :2]
        center = np.mean(xy, axis=0)
        dists = np.linalg.norm(xy - center, axis=1)

        # 在距离分布中找间隙 (孔的特征: 内圈顶点缺失)
        sorted_d = np.sort(dists)
        if len(sorted_d) < 20:
            continue

        # 找相邻距离跳变 > 1.5mm 的位置
        gaps = np.diff(sorted_d)
        threshold = 1.5
        for idx in np.where(gaps > threshold)[0]:
            hole_r = float(sorted_d[idx])
            if hole_r > 2.0 and hole_r < float(np.max(dists)) * 0.9:
                holes.append({
                    "face": label,
                    "type": "through_hole",
                    "radius": round(hole_r, 2),
                    "diameter": round(hole_r * 2, 2),
                })
                break  # 每面只取最大间隙

    return holes


def estimate_wall_thickness(mesh: trimesh.Trimesh, size: np.ndarray) -> float | None:
    """粗略估计壁厚; 实心件返回 None"""
    dx, dy, dz = size[0], size[1], size[2]
    bbox_vol = dx * dy * dz
    mesh_vol = float(mesh.volume) if hasattr(mesh, "volume") else 0
    # 体积 > 包围盒 50% → 实心
    if bbox_vol > 0 and mesh_vol > bbox_vol * 0.5:
        return None

    x_min, x_max = mesh.bounds[0][0], mesh.bounds[1][0]
    mid_x = (x_min + x_max) / 2
    slice_tol = 0.5
    mid_verts = mesh.vertices[
        (mesh.vertices[:, 0] > mid_x - slice_tol) &
        (mesh.vertices[:, 0] < mid_x + slice_tol)
    ]
    if len(mid_verts) > 2:
        y_span = np.max(mid_verts[:, 1]) - np.min(mid_verts[:, 1])
        wall = (dy - y_span) / 2
        if wall > 0.3:
            return round(float(wall), 2)
    return None


# ============================================================================
# STEP 重建 (仅当 cadquery 可用)
# ============================================================================

def build_step(mesh: trimesh.Trimesh, dims: dict, out_path: Path) -> bool:
    """用 cadquery 重建并导出 STEP"""
    if not CADQUERY_OK:
        return False

    ptype = dims["part_type"]
    dx, dy, dz = dims["size_x"], dims["size_y"], dims["size_z"]

    try:
        if ptype == "round_disk":
            r = dims["profile_radius"]
            result = cq.Workplane("XY").circle(r).extrude(dz)
            # 打中心孔 (如果有)
            for h in dims.get("holes", []):
                result = result.faces(">Z").workplane().hole(h["diameter"], dz)
        elif ptype == "flat_plate":
            result = cq.Workplane("XY").box(dx, dy, dz, centered=True)
        elif ptype in ("box", "shallow_box"):
            wall = dims.get("wall_thickness", 2.0)
            result = (
                cq.Workplane("XY").box(dx, dy, dz, centered=True)
                .faces(">Z").workplane()
                .rect(dx - wall * 2, dy - wall * 2)
                .cutBlind(-(dz - wall))
            )
        elif ptype == "cylinder":
            r = dims["profile_radius"]
            result = cq.Workplane("XY").circle(r).extrude(dz)
        else:
            return False

        cq.exporters.export(result, str(out_path))
        return True
    except Exception:
        return False


# ============================================================================
# 报告输出
# ============================================================================

def print_dimensions(name: str, dims: dict):
    """终端输出尺寸摘要"""
    print(f"\n{'─'*50}")
    print(f"📐 {name}")
    print(f"   类型: {dims['part_type']}")
    print(f"   外形: {dims['size_x']} × {dims['size_y']} × {dims['size_z']} mm")
    print(f"   体积: {dims['volume']:.1f} mm³")
    print(f"   投影半径: {dims['profile_radius']} mm")

    holes = dims.get("holes", [])
    if holes:
        print(f"   孔洞 ({len(holes)}):")
        for h in holes:
            print(f"     - {h['face']}: ⌀{h['diameter']} mm")
    else:
        print(f"   孔洞: 无")

    if dims.get("wall_thickness") is not None:
        print(f"   壁厚: ~{dims['wall_thickness']} mm")


def write_csv(all_dims: list[dict]):
    """写 CSV 汇总表"""
    STEP_DIR.mkdir(exist_ok=True)
    with open(CSV_PATH, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(["零件名", "类型", "X(mm)", "Y(mm)", "Z(mm)",
                     "体积(mm³)", "投影半径(mm)", "孔数", "孔径(mm)", "壁厚(mm)"])
        for d in all_dims:
            holes = d.get("holes", [])
            hole_str = "; ".join(f"{h['face']}:⌀{h['diameter']}" for h in holes)
            wall = d.get("wall_thickness", "")
            w.writerow([
                d["name"], d["part_type"],
                d["size_x"], d["size_y"], d["size_z"],
                d["volume"], d["profile_radius"],
                d["hole_count"], hole_str, wall,
            ])
    print(f"\n📋 CSV 尺寸表: {CSV_PATH}")


def write_detail_report(name: str, dims: dict, path: Path):
    """写单个详细报告"""
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"# {name} 尺寸报告\n")
        f.write(f"生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M')}\n\n")
        f.write(f"## 基本信息\n")
        f.write(f"- 类型: {dims['part_type']}\n")
        f.write(f"- 外形: {dims['size_x']} × {dims['size_y']} × {dims['size_z']} mm\n")
        f.write(f"- 体积: {dims['volume']:.2f} mm³\n")
        f.write(f"- 表面积: {dims['area']:.2f} mm²\n")
        f.write(f"- 顶点/面: {dims['vertices']} / {dims['faces']}\n\n")
        f.write(f"## 包围盒\n")
        f.write(f"- Min: [{dims['bbox_lo'][0]:.2f}, {dims['bbox_lo'][1]:.2f}, {dims['bbox_lo'][2]:.2f}]\n")
        f.write(f"- Max: [{dims['bbox_hi'][0]:.2f}, {dims['bbox_hi'][1]:.2f}, {dims['bbox_hi'][2]:.2f}]\n\n")
        f.write(f"## 投影\n")
        f.write(f"- 外接圆半径: {dims['profile_radius']:.2f} mm\n\n")
        f.write(f"## 孔洞\n")
        holes = dims.get("holes", [])
        if holes:
            for h in holes:
                f.write(f"- {h['face']}面: ⌀{h['diameter']} mm (R{h['radius']})\n")
        else:
            f.write(f"- 未检测到\n")
        if dims.get("wall_thickness") is not None:
            f.write(f"\n## 壁厚\n- ~{dims['wall_thickness']} mm\n")


# ============================================================================
# 主流程
# ============================================================================

def process_one(stl_path: Path) -> dict | None:
    """处理单个 STL, 返回尺寸字典"""
    name = stl_path.stem
    print_dim_header = True

    mesh = load_and_merge(stl_path)
    if mesh is None:
        return None

    dims = extract_all(mesh)
    dims["name"] = name
    dims["stl_path"] = str(stl_path)

    print_dimensions(name, dims)

    # 详细报告
    detail_path = STEP_DIR / f"{name}_dims.txt"
    write_detail_report(name, dims, detail_path)

    # 尝试 STEP
    step_path = STEP_DIR / f"{name}.step"
    if CADQUERY_OK:
        ok = build_step(mesh, dims, step_path)
        if ok:
            print(f"   ✅ STEP: {step_path.name}")
        else:
            print(f"   ⚠ STEP 重建不支持此类型, 请用尺寸报告人工重建")
    else:
        print(f"   💡 安装 cadquery (Python≤3.11) 可自动生成 STEP")

    return dims


def main():
    parser = argparse.ArgumentParser(description="STL 尺寸分析 & STEP 转换")
    parser.add_argument("--single", type=str, help="单个文件")
    parser.add_argument("--csv", action="store_true", help="仅生成 CSV")
    parser.add_argument("--no-step", action="store_true", help="跳过 STEP")
    args = parser.parse_args()

    STEP_DIR.mkdir(exist_ok=True)

    if args.single:
        matches = list(STL_DIR.glob(f"*{args.single}*"))
        if not matches:
            print(f"❌ 找不到: {args.single}")
            sys.exit(1)
        process_one(matches[0])
        return

    stl_files = sorted(set(STL_DIR.glob("*.STL")) | set(STL_DIR.glob("*.stl")))
    if not stl_files:
        print(f"❌ {STL_DIR} 下没有 STL 文件")
        sys.exit(1)

    print(f"\n🔍 分析 {len(stl_files)} 个 STL 文件...")

    all_dims = []
    for stl_path in stl_files:
        dims = process_one(stl_path)
        if dims:
            all_dims.append(dims)

    # CSV 汇总
    write_csv(all_dims)

    print(f"\n{'='*55}")
    print(f"✅ 完成: {len(all_dims)} 个零件")
    print(f"   详细报告: {STEP_DIR}/*_dims.txt")
    print(f"   CSV 汇总: {CSV_PATH}")
    if CADQUERY_OK:
        print(f"   STEP 文件: {STEP_DIR}/*.step")
    else:
        print(f"\n💡 要生成 STEP, 请用 Python ≤3.11 + 'pip install cadquery'")
        print(f"   或参考尺寸报告在 Fusion 360/FreeCAD 中重建")


if __name__ == "__main__":
    main()

