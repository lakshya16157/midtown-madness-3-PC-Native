"""The street graph of a city, from the game's own road data.

tools/races/data/<City>.json is the graph mm3_race_guidance.c builds from the
city's city.ai when a race loads (written once to logs/city-roads-*.json):
junction positions, each road's sampled centre line and every edge with its
road and width. This module turns it into junctions joined by roads, for
designing circuits: lengths, headings, which roads meet at each junction.
"""
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent


class Road:
    def __init__(self, index, points, width):
        self.index = index
        self.points = points          # [(x, y, z)] from its start to its end
        self.width = width
        self.ends = [None, None]      # junction (or road-end node) at start / end
        self.length = sum(math.dist(points[i], points[i + 1]) for i in range(len(points) - 1))


class Graph:
    def __init__(self, data):
        nodes = [tuple(p) for p in data["nodes"]]
        junctions = data["junctions"]
        self.junctions = nodes[:junctions]
        self.roads = []
        road_of_node = {}
        for index, (first, last) in enumerate(data["road_nodes"]):
            for node in range(first, last + 1):
                road_of_node[node] = index
        widths = {}
        links = []
        for a, b, road, width in data["edges"]:
            ra, rb = road_of_node.get(a), road_of_node.get(b)
            if ra is not None and ra == rb:
                widths.setdefault(ra, []).append(width)
            else:
                links.append((a, b))
        for index, (first, last) in enumerate(data["road_nodes"]):
            w = widths.get(index, [6.0])
            self.roads.append(Road(index, nodes[first:last + 1], sorted(w)[len(w) // 2]))
        self.first = {road.index: data["road_nodes"][road.index][0] for road in self.roads}
        self.last = {road.index: data["road_nodes"][road.index][1] for road in self.roads}
        # A road end joins a junction, or another road's end directly. Merge
        # directly joined ends into one meeting point ("node" = junction id or
        # ('end', node)).
        parent = {}

        def find(x):
            while parent.get(x, x) != x:
                parent[x] = parent.get(parent[x], parent[x])
                x = parent[x]
            return x

        def union(a, b):
            ra, rb = find(a), find(b)
            if ra != rb:
                parent[rb] = ra
        end_nodes = set(self.first.values()) | set(self.last.values())
        for a, b in links:
            ka = ("j", a) if a < junctions else ("n", a)
            kb = ("j", b) if b < junctions else ("n", b)
            if (a < junctions or a in end_nodes) and (b < junctions or b in end_nodes):
                union(ka, kb)
        groups = {}
        for road in self.roads:
            for side, node in ((0, self.first[road.index]), (1, self.last[road.index])):
                key = find(("n", node))
                road.ends[side] = key
                groups.setdefault(key, []).append((road.index, side))
        self.meetings = groups      # meeting point -> [(road, side)]
        self.position = {}
        for key, members in groups.items():
            pts = [self.roads[r].points[0 if s == 0 else -1] for r, s in members]
            self.position[key] = tuple(sum(p[i] for p in pts) / len(pts) for i in range(3))
        # Stable small numbers for meeting points (the map labels).
        self.ids = sorted(groups, key=str)
        self.number = {key: n for n, key in enumerate(self.ids)}

    def neighbours(self, meeting):
        """[(road, side at this meeting, other meeting)]"""
        out = []
        for road_index, side in self.meetings[meeting]:
            road = self.roads[road_index]
            out.append((road_index, side, road.ends[1 - side]))
        return out


def load(city):
    return Graph(json.loads((HERE / "data" / f"{city}.json").read_text()))


def heading(a, b):
    """Game rotation facing from a to b: a car at rotation r faces (sin r, cos r)."""
    r = math.atan2(b[0] - a[0], b[2] - a[2])
    return r if r >= 0 else r + 2 * math.pi
