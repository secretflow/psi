# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "sparsehash",
    hdrs = glob([
        "src/google/**/*",
        "src/sparsehash/**/*",
    ]),
    includes = ["src"],
    visibility = ["//visibility:public"],
    deps = [
        "@psi//psi/rr22:sparsehash_config",
    ],
)
